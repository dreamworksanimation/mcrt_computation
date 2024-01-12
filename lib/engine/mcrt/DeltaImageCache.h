// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <mcrt_dataio/engine/merger/FbMsgMultiChans.h>
#include <mcrt_messages/ProgressiveFrame.h>
#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Fb.h>
#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/math/Viewport.h>
#include <scene_rdl2/common/rec_time/RecTime.h>
#include <scene_rdl2/render/cache/CacheDequeue.h>
#include <scene_rdl2/render/cache/CacheEnqueue.h>

#include <list>
#include <mutex>

namespace mcrt_computation {

class DeltaImageCacheItem
//
// This is a single cache item entry of single progressiveFrame outgoing message to the
// merge computation.
//
{
public:
    static constexpr unsigned UNDEFINED = ~static_cast<unsigned>(0);

    DeltaImageCacheItem() = default;
    DeltaImageCacheItem(mcrt::ProgressiveFrame::Ptr message, float deltaSec)
        : mDeltaSec(deltaSec)
        , mMessage(message)
    {}
    DeltaImageCacheItem(const DeltaImageCacheItem& src) = default;

    float getDeltaSec() const { return mDeltaSec; }
    unsigned getSendImageActionId() const
    {
        if (!mMessage) return UNDEFINED;
        return mMessage->mSendImageActionId;
    }
    bool isCoarsePass() const
    {
        if (!mMessage) return false;

        constexpr int COARSE_PASS = 0;
        if (mMessage->mCoarsePassStatus == COARSE_PASS) return true;
        return false;
    }

    mcrt::ProgressiveFrame::Ptr getMessage() const { return mMessage; }

    void encode(scene_rdl2::cache::CacheEnqueue& cEnq) const;
    void decode(scene_rdl2::cache::CacheDequeue& cDeq);

    std::string show() const;

private:

    void encodeProgressiveFrame(scene_rdl2::cache::CacheEnqueue& cEnq) const;
    void decodeProgressiveFrame(scene_rdl2::cache::CacheDequeue& cDeq);

    //------------------------------

    float mDeltaSec {0.0f};           // time delta from DeltaImageCache construction
    mcrt::ProgressiveFrame::Ptr mMessage {nullptr};
};

class DeltaImageCache
//
// There are 2 main functionalities of this class.
// First of all, this class keeps multiple progressiveFrame outgoing data.
// Basically, all outgoing progressiveFrame messages to the merger computation are saved
// into this class by encoded format as progressiveFrame data.
// Cached progressiveFrame data would be decoded if the cached item total exceeded the limit.
// This logic avoids using too much memory for the cache.
// The second functionality is related to feedback image processing.
// This class decodes cache items during progressiveFeedback message processing and creates
// decodedFb and mergedFb information and is used for generating minusOne fb data.
//
{
public:    
    using Arg = scene_rdl2::grid_util::Arg;    
    using Parser = scene_rdl2::grid_util::Parser;

    DeltaImageCache()
    {
        parserConfigure();
        mRecTime.start();
    }

    unsigned getOldestActionId() const { return mSentData.back().getSendImageActionId(); }

    unsigned getWidth() const { return mWidth; }
    unsigned getHeight() const { return mHeight; }

    void reset(const scene_rdl2::math::Viewport& rezedViewport);

    void enqueueMessage(mcrt::ProgressiveFrame::Ptr message); // MTsafe

    float decodeMessage(const std::string& mergeActionEncodedData,
                        std::string& warning); // return latency by sec

    unsigned getDecodedSendImageActionId() const { return mDecodedSendImageActionId; }
    unsigned getLastPartialMergeTileId() const { return mLastPartialMergeTileId; }

    const scene_rdl2::grid_util::Fb& getDecodedFb() const { return mDecodedFb; }
    const scene_rdl2::grid_util::Fb& getMergedFb() const { return mMergedFb; }

    std::string show() const;
    std::string showSentData(bool simple = false) const;

    Parser& getParser() { return mParser; }

private:
    using MsgOutCallBack = std::function<bool(const std::string& message)>;

    inline float calcCachedDataTimeLength() const; // return time length of cache data by sec

    unsigned getAlignedWidth() const { return (mWidth + 7) & ~7; }
    unsigned getAlignedHeight() const { return (mHeight + 7) & ~7; }
    unsigned getNumTileX() const { return getAlignedWidth() >> 3; }
    unsigned getNumTileY() const { return getAlignedHeight() >> 3; }
    unsigned getNumTile() const { return getNumTileX() * getNumTileY(); }

    float decodeSingleItemById(unsigned sendImageActionId, std::string& error);  // return latency by sec
    void decodeSingleItem(const DeltaImageCacheItem& item);

    void mergeTiles(const scene_rdl2::grid_util::Fb::PartialMergeTilesTbl& tileTbl,
                    unsigned lastPartialMergeTileId);


    const DeltaImageCacheItem* findItem(const size_t sendImageActionId) const;

    void parserConfigure();
    bool cmdDecodeSingleItem(const unsigned sendImageActionId, const MsgOutCallBack& msgOut);
    bool cmdSaveSentData(const std::string& filename, const MsgOutCallBack& msgOut) const;
    bool cmdLoadSentData(const std::string& filename, const MsgOutCallBack& msgOut);
    bool cmdDecodeAndSavePPM(const unsigned startId, const unsigned endId, const std::string& filename,
                             const MsgOutCallBack& msgOut);

    //------------------------------

    scene_rdl2::rec_time::RecTime mRecTime;

    // If this bool is true, DeltaImageCache does not work for feedback logic and changes its behavior to
    // progressiveFrame data debugging purposes.
    bool mDebugMode {false};

    float mMaxCachedDataTimeLength {10.0f}; // sec
    mutable std::mutex mSentDataMutex;
    std::list<DeltaImageCacheItem> mSentData;

    unsigned mDecodedSendImageActionId {DeltaImageCacheItem::UNDEFINED}; // last decoded sendImageActionId
    unsigned mLastPartialMergeTileId {0}; // last partial merge tileId
    unsigned mWidth {0}; // internal framebuffer width
    unsigned mHeight {0}; // internal framebuffer height
    
    mcrt_dataio::FbMsgMultiChans mMultiChanDecoder;
    scene_rdl2::grid_util::Fb mDecodedFb; // framebuffer for decoded message
    scene_rdl2::grid_util::Fb mMergedFb; // framebuffer for merged operation

    Parser mParser;    
};

inline float
DeltaImageCache::calcCachedDataTimeLength() const
{
    if (mSentData.size() <= 1) return 0.0f;
    return mSentData.front().getDeltaSec() - mSentData.back().getDeltaSec();
}

} // namespace mcrt_computation
