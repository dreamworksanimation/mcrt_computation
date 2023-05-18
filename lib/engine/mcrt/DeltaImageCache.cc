// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#include "DeltaImageCache.h"

#include <mcrt_dataio/engine/merger/MergeSequenceDequeue.h>

//#define DEBUG_MSG

namespace mcrt_computation {

std::string
DeltaImageCacheItem::show() const
{
    std::ostringstream ostr;
    ostr << "mDeltaSec:" << std::setw(7) << std::fixed << std::setprecision(4) << getDeltaSec()
         << " sendImageActionId:" << getSendImageActionId();
    return ostr.str();
}

//------------------------------------------------------------------------------------------

void
DeltaImageCache::reset(const scene_rdl2::math::Viewport& rezedViewport)
{
    mSentData.clear();

    mMultiChanDecoder.reset();
    mDecodedFb.init(rezedViewport);
    mMergedFb.init(rezedViewport);

    mWidth = mDecodedFb.getWidth();
    mHeight = mDecodedFb.getHeight();
}

void
DeltaImageCache::enqueueMessage(mcrt::ProgressiveFrame::Ptr message) // MTsafe
{
    std::lock_guard<std::mutex> lock(mSentDataMutex);

#   ifdef DEBUG_MSG
    std::ostringstream ostr;
    ostr << ">> DeltaImageCache.cc enqueueMessage() actionId:" << message->mSendImageActionId;
    if (message->getStatus() == mcrt::BaseFrame::STARTED) ostr << " STARTED";
    std::cerr << ostr.str() << '\n';
#   endif // end DEBUG_MSG

    if (message->getStatus() == mcrt::BaseFrame::STARTED) {
        // We don't need to keep old data anymore. clean up here.
        reset(scene_rdl2::math::Viewport(message->getRezedViewport().minX(),
                                         message->getRezedViewport().minY(),
                                         message->getRezedViewport().maxX(),
                                         message->getRezedViewport().maxY()));
    }

    mSentData.emplace_front(message, mRecTime.end());
    if (mSentData.size() <= 1) return;

    // Safety logic for keeping internal memory usage moderate.
    // We decode old stored message and save as framebuffer data.
    while (true) {
        if (calcCachedDataTimeLength() <= mMaxCachedDataTimeLength) break;

        decodeSingleItem(mSentData.back());
        mSentData.pop_back();
    }
}

float
DeltaImageCache::decodeMessage(const std::string& mergeActionEncodedData,
                                      std::string& warning)
{
#   ifdef DEBUG_MSG
    std::cerr << ">> DeltaImageCache.cc decodeMessage() start {\n"
              << scene_rdl2::str_util::addIndent(scene_rdl2::rdl2::ValueContainerUtil::hexDump("mergeActionEncodedData",
                                                                                               mergeActionEncodedData.data(),
                                                                                               mergeActionEncodedData.size())) << '\n'
              << "}\n";
#   endif // end DEBUG_MSG

    mcrt_dataio::MergeSequenceDequeue deq(mergeActionEncodedData.data(), mergeActionEncodedData.size());
    warning.clear();

    mcrt_dataio::MergeSequenceKey lastAction = mcrt_dataio::MergeSequenceKey::EOD;
    auto priorActionWasMerge = [&]() {
        return (lastAction == mcrt_dataio::MergeSequenceKey::MERGE_TILE_SINGLE ||
                lastAction == mcrt_dataio::MergeSequenceKey::MERGE_TILE_RANGE ||
                lastAction == mcrt_dataio::MergeSequenceKey::MERGE_ALL_TILES);
    };
    auto priorActionWasDecode = [&]() {
        return (lastAction == mcrt_dataio::MergeSequenceKey::DECODE_SINGLE ||
                lastAction == mcrt_dataio::MergeSequenceKey::DECODE_RANGE);
    };

    scene_rdl2::grid_util::Fb::PartialMergeTilesTbl tileTbl;
    tileTbl.resize(getNumTile());
    auto tileTblSet = [&](unsigned tileId, bool flag) { tileTbl[tileId] = static_cast<char>(flag); };
    auto tileTblSetRange = [&](unsigned startTileId, unsigned endTileId, bool flag) {
        for (unsigned tileId = startTileId; tileId <= endTileId; ++tileId) { tileTblSet(tileId, flag); }
    };
    auto tileTblSetAll = [&](bool flag) { tileTblSetRange(0, getNumTile() - 1, flag); };

    float currTimeStampSec = mRecTime.end(); // current timestamp from beginning
    float latestTimeStampSec = 0.0f; // latest decode data timestamp from beginning
    auto updateTimeStamp = [&](float sec) { latestTimeStampSec = sec; };

    unsigned lastPartialMergeTileId = 0;

    //
    // This is the main logic of MergeAction encoded data decode action.
    // Finally, this would simulate the merge action of Merge computation and will generate
    // decodedFb and mergedFb. Decode action is simply executed when the decode item comes up.
    // However, merge action implementation is a bit complicated. Merge action requires
    // generating a tileTable before the actual merge operation and this processing tileTable
    // is costly if we process each mergeTile action independently.
    // In order to minimize mergeTile processing overhead, the tileTable generation phase is
    // accumulated on comsecutive mergeTile actions and it processes at once just before when
    // we get non-merge tile actions. This solution reduces a tileTable processing overhead as
    // minimized.
    //
    tileTblSetAll(false);
    if (!deq.decodeLoop
        (warning,
         [&](unsigned sendImageActionId) -> bool { // decodeSingleFunc
            if (priorActionWasMerge()) mergeTiles(tileTbl, lastPartialMergeTileId);
#           ifdef DEBUG_MSG
            std::cerr << ">> DeltaImageCache.cc decodeMessage() decodeSingle"
                      << " sendImageActionId:" << sendImageActionId << '\n';
#           endif // end DEBUG_MSG
            float lastTimeStampSec = decodeSingleItemById(sendImageActionId, warning);
            updateTimeStamp(lastTimeStampSec);
            lastAction = mcrt_dataio::MergeSequenceKey::DECODE_SINGLE;
            return true;
         },
         [&](unsigned startSendImageActionId, unsigned endSendImageActionId) -> bool { // decodeRangeFunc
             if (priorActionWasMerge()) mergeTiles(tileTbl, lastPartialMergeTileId);
#            ifdef DEBUG_MSG
             std::cerr << ">> DeltaImageCache.cc decodeMessage() decodeRangeFunc"
                       << " startSendImageActionId:" << startSendImageActionId
                       << " endSendImageActionId:" << endSendImageActionId << '\n';
#            endif // end DEBUG_MSG
             for (unsigned id = startSendImageActionId; id <= endSendImageActionId; ++id) {
                 float lastTimeStampSec = decodeSingleItemById(id, warning);
                 updateTimeStamp(lastTimeStampSec);
             }
             lastAction = mcrt_dataio::MergeSequenceKey::DECODE_RANGE;
             return true;
         },
         [&](unsigned tileId) -> bool { // mergeTileSingleFunc
             if (priorActionWasDecode()) tileTblSetAll(false);
#            ifdef DEBUG_MSG
             std::cerr << ">> DeltaImageCache.cc decodeMessage() mergeTileSingle"
                       << " tileId:" << tileId << '\n';
#            endif // end DEBUG_MSG
             tileTblSet(tileId, true);
             lastPartialMergeTileId = tileId;
             lastAction = mcrt_dataio::MergeSequenceKey::MERGE_TILE_SINGLE;
             return true;
         },
         [&](unsigned startTileId, unsigned endTileId) -> bool { // mergeTileRangeFunc
             if (priorActionWasDecode()) tileTblSetAll(false);
#            ifdef DEBUG_MSG
             std::cerr << ">> DeltaImageCache.cc decodeMessage()"
                       << " mergeTileRange startTileId:" << startTileId << " endTileId:" << endTileId << '\n';
#            endif // end DEBUG_MSG
             tileTblSetRange(startTileId, endTileId, true);
             lastPartialMergeTileId = endTileId;
             lastAction = mcrt_dataio::MergeSequenceKey::MERGE_TILE_RANGE;
             return true;
         },
         [&]() -> bool { // mergeAllTilesFunc
#            ifdef DEBUG_MSG
             std::cerr << ">> DeltaImageCache.cc decodeMessage() mergeAllTiles\n";
#            endif // end DEBUG_MSG
             if (priorActionWasDecode()) tileTblSetAll(false);
             tileTblSetAll(true);
             lastPartialMergeTileId = 0; // special case
             lastAction = mcrt_dataio::MergeSequenceKey::MERGE_ALL_TILES;
             return true;
         },
         [&]() -> bool { // eodFunc
             if (priorActionWasMerge()) mergeTiles(tileTbl, lastPartialMergeTileId);
#            ifdef DEBUG_MSG
             std::cerr << ">> DeltaImageCache.cc decodeMessage() endOfData\n";
#            endif // end DEBUG_MSG
             lastAction = mcrt_dataio::MergeSequenceKey::EOD;
             return true;
         })) {
        warning += "WARNING : There was a mergeActionEncodeData decode problem. ";
    }

#   ifdef DEBUG_MSG
    std::cerr << ">> DeltaImageCache.cc decodeMessage() finish\n";
#   endif // end DEBUG_MSG

    if (latestTimeStampSec == 0.0f) return 0.0f; // no decoded data processed
    return currTimeStampSec - latestTimeStampSec; // latency sec
}

std::string    
DeltaImageCache::show() const
{
    using scene_rdl2::str_util::secStr;

    std::ostringstream ostr;
    ostr << "DeltaImageCache {\n"
         << "  mMaxCachedDataTimeLength:" << mMaxCachedDataTimeLength
         << " (" << secStr(mMaxCachedDataTimeLength) << ")\n"
         << scene_rdl2::str_util::addIndent(showSentData(true)) << '\n'
         << "  mDecodedSendImageActionId:" << mDecodedSendImageActionId << '\n'
         << "  mLastPartialMergeTileId:" << mLastPartialMergeTileId << '\n'
         << "  mWidth:" << mWidth << '\n'
         << "  mHeight:" << mHeight << '\n'
         << "}";
    return ostr.str();
}

std::string
DeltaImageCache::showSentData(bool simple) const // MTsafe
{
    std::lock_guard<std::mutex> lock(mSentDataMutex);

    if (mSentData.size() == 0) {
        return std::string("sentData is empty");
    }

    int w0 = scene_rdl2::str_util::getNumberOfDigits(static_cast<unsigned>(mSentData.size()));
    int w1 = scene_rdl2::str_util::getNumberOfDigits(mSentData.front().getSendImageActionId());

    std::ostringstream ostr;
    ostr << "sentData (size:" << mSentData.size() << ") {\n";
    unsigned id = 0;
    for (auto itr = mSentData.begin(); itr != mSentData.end(); ++itr) {
        bool flag = true;
        if (simple) {
            flag = (id == 0 || id == mSentData.size() - 1);
        }
        if (flag) {
            ostr << "  id:" << std::setw(w0) << id
                 << " deltaSec:" << std::setw(7) << std::fixed << std::setprecision(4) << itr->getDeltaSec()
                 << " sendImageActionId:" << std::setw(w1) << itr->getSendImageActionId() << '\n';
        }
        id++;
    }
    ostr << "} deltaTimeLength:" << calcCachedDataTimeLength() << " sec";
    return ostr.str();
}

float
DeltaImageCache::decodeSingleItemById(unsigned targetSendImageActionId,
                                      std::string& warning)
{
#   ifdef DEBUG_MSG
    std::cerr << ">> DeltaImageCache.cc decodeSingleItemById()"
              << " targetSendImageActionId:" << targetSendImageActionId
              << " mSentData.size():" << mSentData.size()
              << '\n';
#   endif // end DEBUG_MSG

    DeltaImageCacheItem currItem;
    unsigned currSendImageActionId;
    auto getItem = [&]() {
        std::lock_guard<std::mutex> lock(mSentDataMutex);
        if (mSentData.size() == 0) {
            // sent data empty this might happen when just after start frame
            return -1;
        }
        currSendImageActionId = mSentData.back().getSendImageActionId();
        if (currSendImageActionId > targetSendImageActionId) {
            // Could not find data associated with targetSendImageActionId data.
            // Probably targetSendImageActionId is too old.
            // We might get START condition snapshot shortly. This is not a bug. -> Just skip this data
            return -1;
        } else if (currSendImageActionId == targetSendImageActionId) {
            // We found target. Copy data to currItem and remove from mSentData list.
            currItem = mSentData.back();
            mSentData.pop_back();
            return 0;
        } else {
            // Somehow old data is there. This item should be processed already.
            // This is potential error but trying to continue.
            mSentData.pop_back();
            return 1;
        }
    };
    auto pushWarning = [&](const std::string& msg) {
        if (!warning.empty()) warning += '\n';
        warning += msg;
    };

    float lastTimeStampSec = 0.0f; // time delta from DeltaImageCache construction

    while (true) {
        int getItemResult = getItem();
        if (getItemResult == -1) {
            // Could not find data associated with targetSendImageActionId
#           ifdef DEBUG_MSG
            std::cerr << ">> DeltaImageCache.cc decodeSingleItemById() skip\n";
#           endif // end DEBUG_MSG
            break;
        } else if (getItemResult == 0) {
#           ifdef DEBUG_MSG
            std::cerr << ">> DeltaImageCache.cc decodeSingleItemById() found\n";
#           endif // end DEBUG_MSG
            // found target
            decodeSingleItem(currItem);
            lastTimeStampSec = currItem.getDeltaSec();
            break;
        } else {
#           ifdef DEBUG_MSG
            std::cerr << ">> DeltaImageCache.cc decodeSingleItemById() potential error\n";
#           endif // end DEBUG_MSG
            std::ostringstream ostr;
            ostr << "WARNING : wasted data found. sendImageActionId:" << currSendImageActionId << " is not merged.";
            pushWarning(ostr.str());
        }
    }

#   ifdef DEBUG_MSG
    std::cerr << ">> DeltaImageCache.cc decodeSingleItemById() done\n";
#   endif // end DEBUG_MSG

    return lastTimeStampSec; // 0.0 if error
}

void
DeltaImageCache::decodeSingleItem(const DeltaImageCacheItem& item)
{
    if (item.getMessage()->getStatus() == mcrt::BaseFrame::STARTED) {
        mDecodedFb.reset();
        mMergedFb.reset();
#       ifdef DEBUG_MSG
        std::cerr << "DeltaImageCache.cc decodeSingleItem() STARTED actionId:" << item.getSendImageActionId() << "\n";
#       endif // end DEBUG_MSG
    }

    if (!mMultiChanDecoder.push(/* delayDecode = */ false,
                                *(item.getMessage()),
                                mDecodedFb,
                                /* parallel = */ false,
                                /* skipLatencyLog = */ true)) {
        std::cerr << "DeltaImageCache.cc ERROR : decode() failed.\n";
    }
    mDecodedSendImageActionId = item.getSendImageActionId();
}

void
DeltaImageCache::mergeTiles(const scene_rdl2::grid_util::Fb::PartialMergeTilesTbl& partialMergeTiles,
                            unsigned lastPartialMergeTileId)
{
#   ifdef DEBUG_MSG
    std::cerr << ">> DeltaImageCache.cc mergeTiles() "
              << scene_rdl2::grid_util::Fb::showPartialMergeTilesTbl(partialMergeTiles) << '\n';
#   endif // end DEBUG_MSG
        
    mMergedFb.copyRenderBuffer(&partialMergeTiles, mDecodedFb);
    mLastPartialMergeTileId = lastPartialMergeTileId;
}

void
DeltaImageCache::parserConfigure()
{
    mParser.description("DeltaImageCache command");
    mParser.opt("maxCachedTimeLength", "<sec|show>", "set max cached data time length",
                [&](Arg& arg) {
                    if ((arg)() != "show") mMaxCachedDataTimeLength = (arg++).as<float>(0);
                    else arg++;
                    return arg.msg(std::to_string(mMaxCachedDataTimeLength) + " sec\n");
                });
    mParser.opt("show", "", "show internal info",
                [&](Arg& arg) { return arg.msg(show() + '\n'); });
    mParser.opt("showSentData", "", "show sentData internal info. (might be pretty long message)",
                [&](Arg& arg) { return arg.msg(showSentData() + '\n'); });
    mParser.opt("decodedFb", "...command...", "internal decodedFb command",
                [&](Arg& arg) { return mDecodedFb.getParser().main(arg.childArg()); });
    mParser.opt("multiChanDecorder", "...command...", "multiChanDecoder command",
                [&](Arg& arg) { return mMultiChanDecoder.getParser().main(arg.childArg()); });
}

} // namespace mcrt_computation
