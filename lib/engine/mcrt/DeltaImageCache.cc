// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "DeltaImageCache.h"

#include <mcrt_dataio/engine/merger/MergeSequenceDequeue.h>
#include <scene_rdl2/render/util/StrUtil.h>

#include <fstream>

//#define DEBUG_MSG

namespace mcrt_computation {

void
DeltaImageCacheItem::encode(scene_rdl2::cache::CacheEnqueue& cEnq) const
{
    cEnq.enqFloat(mDeltaSec);
    encodeProgressiveFrame(cEnq);
}

void
DeltaImageCacheItem::decode(scene_rdl2::cache::CacheDequeue& cDeq)
{
    mDeltaSec = cDeq.deqFloat();
    decodeProgressiveFrame(cDeq);
}

std::string
DeltaImageCacheItem::show() const
{
    std::ostringstream ostr;
    ostr << "mDeltaSec:" << std::setw(7) << std::fixed << std::setprecision(4) << getDeltaSec()
         << " sendImageActionId:" << getSendImageActionId();
    return ostr.str();
}

void
DeltaImageCacheItem::encodeProgressiveFrame(scene_rdl2::cache::CacheEnqueue& cEnq) const
{
    auto encodeViewport = [](scene_rdl2::cache::CacheEnqueue& cEnq, mcrt::BaseFrame::Viewport& vp) {
        cEnq.enqVLInt(vp.mMinX);
        cEnq.enqVLInt(vp.mMinY);
        cEnq.enqVLInt(vp.mMaxX);
        cEnq.enqVLInt(vp.mMaxY);
        cEnq.enqBool(vp.mHasViewport);
    };
    auto encodeDataBuffer = [](scene_rdl2::cache::CacheEnqueue& cEnq, mcrt::BaseFrame::DataBuffer& db) {
        cEnq.enqVLUInt(db.mDataLength);
        if (db.mDataLength) {
            cEnq.enqByteData(db.mData.get(), db.mDataLength);
        }
        std::string name("");
        if (db.mName) name = std::string(db.mName);
        cEnq.enqString(name);
        cEnq.enqVLInt(static_cast<int>(db.mType));
    };

    if (!mMessage) {
        cEnq.enqBool(false);
        return; // early exit
    }

    cEnq.enqBool(true);

    //------------------------------
    //
    // progressiveFrame message header
    //
    cEnq.enqVLInt(mMessage->mMachineId); // int 32bit
    cEnq.enqVLUInt(mMessage->mSnapshotId); // uint32_t 32bit
    cEnq.enqVLUInt(mMessage->mSendImageActionId); // uint32_t 32bit
    cEnq.enqVLULong(mMessage->mSnapshotStartTime); // uint64_t 64bit
    cEnq.enqVLInt(mMessage->mCoarsePassStatus); // int 32bit

    cEnq.enqString(mMessage->mDenoiserAlbedoInputName);
    cEnq.enqString(mMessage->mDenoiserNormalInputName);

    cEnq.enqVLSizeT(mMessage->mHeader.mNumBuffers);
    cEnq.enqVLSizeT(mMessage->mHeader.mViewId);
    encodeViewport(cEnq, mMessage->mHeader.mRezedViewport);
    encodeViewport(cEnq, mMessage->mHeader.mViewport);
    cEnq.enqVLUInt(mMessage->mHeader.mFrameId);
    cEnq.enqVLInt(static_cast<int>(mMessage->mHeader.mStatus));
    cEnq.enqFloat(mMessage->mHeader.mProgress);

    //------------------------------
    //
    // progressiveFrame mBuffers
    //
    cEnq.enqVLSizeT(mMessage->mBuffers.size());
    for (auto& itr : mMessage->mBuffers) {
        encodeDataBuffer(cEnq, itr);
    }
}

void
DeltaImageCacheItem::decodeProgressiveFrame(scene_rdl2::cache::CacheDequeue& cDeq)
{
    auto decodeViewport = [](scene_rdl2::cache::CacheDequeue& cDeq, mcrt::BaseFrame::Viewport& vp) {
        vp.mMinX = cDeq.deqVLInt();
        vp.mMinY = cDeq.deqVLInt();
        vp.mMaxX = cDeq.deqVLInt();
        vp.mMaxY = cDeq.deqVLInt();
        vp.mHasViewport = cDeq.deqBool();
    };
    auto decodeDataBuffer = [](scene_rdl2::cache::CacheDequeue& cDeq, mcrt::BaseFrame::DataBuffer& db) {
        db.mDataLength = cDeq.deqVLUInt();
        if (db.mDataLength) {
            std::string data(db.mDataLength, 0x0);
            cDeq.deqByteData(static_cast<void*>(&data[0]), db.mDataLength);
            db.mData = mcrt::makeCopyPtr(reinterpret_cast<const uint8_t*>(&data[0]), db.mDataLength);
        } else {
            db.mData = nullptr;
        }
        std::string name = cDeq.deqString();
        if (!name.empty()) {
            db.mName = new char[name.size() + 1];
            std::strncpy(db.mName, name.c_str(), name.size());
            db.mName[name.size()] = 0x0;
        } else {
            db.mName = nullptr;
        }
        db.mType = static_cast<mcrt::BaseFrame::ImageEncoding>(cDeq.deqVLInt());
    };

    if (!cDeq.deqBool()) {
        mMessage = nullptr;
        return; // early exit
    }

    mMessage = std::make_shared<mcrt::ProgressiveFrame>();

    //------------------------------
    //
    // progressiveFrame message header
    //
    mMessage->mMachineId = cDeq.deqVLInt(); // int 32bit
    mMessage->mSnapshotId = cDeq.deqVLUInt(); // uint32_t 32bit
    mMessage->mSendImageActionId = cDeq.deqVLUInt(); // uint32_t 32bit
    mMessage->mSnapshotStartTime = cDeq.deqVLULong(); // uint64_t 64bit
    mMessage->mCoarsePassStatus = cDeq.deqVLInt(); // int 32bit

    mMessage->mDenoiserAlbedoInputName = cDeq.deqString();
    mMessage->mDenoiserNormalInputName = cDeq.deqString();

    mMessage->mHeader.mNumBuffers = cDeq.deqVLSizeT();
    mMessage->mHeader.mViewId = cDeq.deqVLSizeT();
    decodeViewport(cDeq, mMessage->mHeader.mRezedViewport);
    decodeViewport(cDeq, mMessage->mHeader.mViewport);
    mMessage->mHeader.mFrameId = cDeq.deqVLUInt();
    mMessage->mHeader.mStatus = static_cast<mcrt::BaseFrame::Status>(cDeq.deqVLInt());
    mMessage->mHeader.mProgress = cDeq.deqFloat();

    //------------------------------
    //
    // progressiveFrame mBuffers
    //
    size_t size = cDeq.deqVLSizeT();
    mMessage->mBuffers.resize(size);
    for (size_t i = 0; i < size; ++i) {
        decodeDataBuffer(cDeq, mMessage->mBuffers[i]);
    }
}

//------------------------------------------------------------------------------------------

void
DeltaImageCache::reset(const scene_rdl2::math::Viewport& rezedViewport)
{
    mSentData.clear();

    mMultiChanDecoder.reset();
    mDecodedFb.init(rezedViewport);
    mMergedFb.init(rezedViewport);

    mDecodedSendImageActionId = DeltaImageCacheItem::UNDEFINED;
    mLastPartialMergeTileId = 0;

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

    if (mDebugMode) {
        // Under debugMode, we only keep data within the user-defined time length from the
        // start frame. This means if you set debugMode = on, all feedback functionality stop
        // working.
        if (calcCachedDataTimeLength() > mMaxCachedDataTimeLength) return;
    }

    mSentData.emplace_front(message, mRecTime.end());
    if (mSentData.size() <= 1) return;

    if (mDebugMode) return; // early exit for debugMode

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
         << "  mDebugMode:" << scene_rdl2::str_util::boolStr(mDebugMode) << '\n'
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
        } else {
            if (mSentData.size() > 3 && id == 1) {
                ostr << "     ~\n";
            }
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

const DeltaImageCacheItem*
DeltaImageCache::findItem(const size_t sendImageActionId) const // MTsafe
{
    std::lock_guard<std::mutex> lock(mSentDataMutex);

    for (auto itr = mSentData.begin(); itr != mSentData.end(); ++itr) {
        if ((*itr).getSendImageActionId() == sendImageActionId) {
            return &(*itr); // found!
        }
    }

    // Could not find target sendImageActionId data
    return nullptr;
}

void
DeltaImageCache::parserConfigure()
{
    namespace str_util = scene_rdl2::str_util;

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
                [&](Arg& arg) { return arg.msg(showSentData(false) + '\n'); });
    mParser.opt("decodedFb", "...command...", "internal decodedFb command",
                [&](Arg& arg) { return mDecodedFb.getParser().main(arg.childArg()); });
    mParser.opt("multiChanDecorder", "...command...", "multiChanDecoder command",
                [&](Arg& arg) { return mMultiChanDecoder.getParser().main(arg.childArg()); });
    mParser.opt("debugMode", "<on|off|show>",
                "set debugMode condition. If this is true, DeltaImageCache does not work for feedback "
                "logic and changes its behavior to progressiveFrame data debugging purposes",
                [&](Arg& arg) {
                    if ((arg)() != "show") mDebugMode = (arg++).as<bool>(0);
                    else arg++;
                    return arg.msg(str_util::boolStr(mDebugMode) + '\n');
                });
    mParser.opt("decode", "<sendImageActionId>", "decode cached data and stored into decodedFb",
                [&](Arg& arg) {
                    return cmdDecodeSingleItem((arg++).as<unsigned>(0),
                                               [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
    mParser.opt("decodeAndSavePPM", "<startId> <endId> <filename>",
                "Decode and save data in sequence. startId, endId are sendImageActionId. "
                "This command creates both beauty and activePixels files separately. "
                "output filename should not include .ppm extension.",
                [&](Arg& arg) {
                    unsigned startId = (arg++).as<unsigned>(0);
                    unsigned endId = (arg++).as<unsigned>(0);
                    std::string filename = (arg++)();
                    return cmdDecodeAndSavePPM(startId, endId, filename,
                                               [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
    mParser.opt("saveSentData", "<filename>", "save all sentData",
                [&](Arg& arg) {
                    return cmdSaveSentData((arg++)(),
                                           [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
    mParser.opt("loadSentData", "<filename>", "load all sentData",
                [&](Arg& arg) {
                    return cmdLoadSentData((arg++)(),
                                           [&](const std::string& msg) { return arg.msg(msg + '\n'); });
                });
}

bool
DeltaImageCache::cmdDecodeSingleItem(const unsigned sendImageActionId,
                                     const MsgOutCallBack& msgOut)
{
    const DeltaImageCacheItem* deltaImageCacheItem = findItem(sendImageActionId);
    if (!deltaImageCacheItem) {
        std::ostringstream ostr;
        ostr << "ERROR : Could not find DeltaImageCacheItem (sendImageActionId:" << sendImageActionId << ")";
        msgOut(ostr.str());
        return false;
    }

    decodeSingleItem(*deltaImageCacheItem);

    std::ostringstream ostr;
    ostr << "decode action (sendImageActionId:" << sendImageActionId
         << " coarsePass:" << scene_rdl2::str_util::boolStr(deltaImageCacheItem->isCoarsePass());
    if (deltaImageCacheItem->getMessage()->mHeader.mStatus == mcrt::BaseFrame::Status::STARTED) {
        ostr << " STARTED";
    }
    ostr << ")";
    return msgOut(ostr.str());
}

bool
DeltaImageCache::cmdSaveSentData(const std::string& filename,
                                 const MsgOutCallBack& msgOut) const
{
    auto dataEncode = [&]() {
        std::lock_guard<std::mutex> lock(mSentDataMutex);    

        std::string data;
        scene_rdl2::cache::CacheEnqueue cEnq(&data);

        cEnq.enqFloat(mMaxCachedDataTimeLength);
        cEnq.enqVLSizeT(mSentData.size());

        for (auto itr = mSentData.begin(); itr != mSentData.end(); ++itr) {
            (*itr).encode(cEnq);
        }

        cEnq.finalize();

        return data;
    };

    std::string encodedData = dataEncode();

    std::ofstream out(filename.c_str(), std::ios::trunc | std::ios::binary);
    if (!out) {
        std::ostringstream ostr;
        ostr << "Could not open file:" << filename << " for writing.";
        msgOut(ostr.str());
        return false;
    }

    size_t dataSize = encodedData.size();
    out.write(reinterpret_cast<char*>(&dataSize), sizeof(size_t));
    out.write(&encodedData[0], dataSize);

    out.close();

    std::ostringstream ostr;
    ostr << "save file:" << filename << " done";
    msgOut(ostr.str());
    
    return true;
}

bool
DeltaImageCache::cmdLoadSentData(const std::string& filename,
                                 const MsgOutCallBack& msgOut)
{
    auto dataDecode = [&](const std::string& data) {
        std::lock_guard<std::mutex> lock(mSentDataMutex);

        mSentData.clear();

        scene_rdl2::cache::CacheDequeue cDeq(static_cast<const void*>(&data[0]), data.size());

        mMaxCachedDataTimeLength = cDeq.deqFloat();
        size_t size = cDeq.deqVLSizeT();
        for (size_t i = 0; i < size; ++i) {
            mSentData.emplace_back();
            mSentData.back().decode(cDeq);
        }
    };

    std::ifstream in(filename.c_str(), std::ios::binary);
    if (!in) {
        std::ostringstream ostr;
        ostr << "Could not open file:" << filename << " for reading.";
        msgOut(ostr.str());
        return false;
    }

    size_t dataSize;
    in.read(reinterpret_cast<char*>(&dataSize), sizeof(size_t));
    std::string data(dataSize, 0x0);

    in.read(&data[0], dataSize);

    in.close();

    std::ostringstream ostr;
    ostr << "Read file:" << filename << " done.";
    msgOut(ostr.str());

    dataDecode(data);

    return true;
}

bool
DeltaImageCache::cmdDecodeAndSavePPM(const unsigned startId,
                                     const unsigned endId,
                                     const std::string& filename,
                                     const MsgOutCallBack& msgOut)
{
    int w = scene_rdl2::str_util::getNumberOfDigits(endId);
    for (unsigned id = startId; id <= endId; ++id) {
        if (!cmdDecodeSingleItem(id, msgOut)) return false;

        std::ostringstream ostr;
        ostr << filename << '_' << std::setw(w) << std::setfill('0') << id;
        std::string name = ostr.str();
        mDecodedFb.saveBeautyPPM(name + "_beautyRGB.ppm", msgOut);
        mDecodedFb.saveBeautyActivePixelsPPM(name + "_beautyActivePixels.ppm", msgOut);
    }
    return true;
}

} // namespace mcrt_computation
