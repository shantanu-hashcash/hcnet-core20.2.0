// Copyright 2014 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/Floodgate.h"
#include "crypto/BLAKE2.h"
#include "crypto/Hex.h"
#include "herder/Herder.h"
#include "main/Application.h"
#include "medida/counter.h"
#include "medida/metrics_registry.h"
#include "overlay/OverlayManager.h"
#include "util/GlobalChecks.h"
#include "util/Logging.h"
#include "util/XDROperators.h"
#include "xdrpp/marshal.h"
#include <Tracy.hpp>
#include <fmt/format.h>

namespace hcnet
{
Floodgate::FloodRecord::FloodRecord(uint32_t ledger, Peer::pointer peer)
    : mLedgerSeq(ledger)
{
    if (peer)
        mPeersTold.insert(peer->toString());
}

Floodgate::Floodgate(Application& app)
    : mApp(app)
    , mFloodMapSize(
          app.getMetrics().NewCounter({"overlay", "memory", "flood-known"}))
    , mSendFromBroadcast(app.getMetrics().NewMeter(
          {"overlay", "flood", "broadcast"}, "message"))
    , mMessagesAdvertised(app.getMetrics().NewMeter(
          {"overlay", "flood", "advertised"}, "message"))
    , mShuttingDown(false)
{
}

// remove old flood records
void
Floodgate::clearBelow(uint32_t maxLedger)
{
    ZoneScoped;
    for (auto it = mFloodMap.cbegin(); it != mFloodMap.cend();)
    {
        if (it->second->mLedgerSeq < maxLedger)
        {
            it = mFloodMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
    mFloodMapSize.set_count(mFloodMap.size());
}

bool
Floodgate::addRecord(HcnetMessage const& msg, Peer::pointer peer, Hash& index)
{
    ZoneScoped;
    index = xdrBlake2(msg);
    if (mShuttingDown)
    {
        return false;
    }
    auto result = mFloodMap.find(index);
    if (result == mFloodMap.end())
    { // we have never seen this message
        mFloodMap[index] = std::make_shared<FloodRecord>(
            mApp.getHerder().trackingConsensusLedgerIndex(), peer);
        mFloodMapSize.set_count(mFloodMap.size());
        TracyPlot("overlay.memory.flood-known",
                  static_cast<int64_t>(mFloodMap.size()));
        return true;
    }
    else
    {
        result->second->mPeersTold.insert(peer->toString());
        return false;
    }
}

// send message to anyone you haven't gotten it from
bool
Floodgate::broadcast(HcnetMessage const& msg, bool force,
                     std::optional<Hash> const& hash)
{
    ZoneScoped;
    if (mShuttingDown)
    {
        return false;
    }
    if (msg.type() == TRANSACTION)
    {
        // Must pass a hash when broadcasting transactions.
        releaseAssert(hash.has_value());
    }
    Hash index = xdrBlake2(msg);

    FloodRecord::pointer fr;
    auto result = mFloodMap.find(index);
    if (result == mFloodMap.end() || force)
    { // no one has sent us this message / start from scratch
        fr = std::make_shared<FloodRecord>(
            mApp.getHerder().trackingConsensusLedgerIndex(), Peer::pointer());
        mFloodMap[index] = fr;
        mFloodMapSize.set_count(mFloodMap.size());
    }
    else
    {
        fr = result->second;
    }
    // send it to people that haven't sent it to us
    auto& peersTold = fr->mPeersTold;

    // make a copy, in case peers gets modified
    auto peers = mApp.getOverlayManager().getAuthenticatedPeers();

    bool broadcasted = false;
    auto smsg = std::make_shared<HcnetMessage const>(msg);
    for (auto peer : peers)
    {
        releaseAssert(peer.second->isAuthenticated());
        bool pullMode = msg.type() == TRANSACTION;
        bool hasAdvert = pullMode && peer.second->peerKnowsHash(hash.value());

        if (peersTold.insert(peer.second->toString()).second && !hasAdvert)
        {
            if (pullMode)
            {
                mMessagesAdvertised.Mark();
                peer.second->queueTxHashToAdvertise(hash.value());
            }
            else
            {
                mSendFromBroadcast.Mark();
                std::weak_ptr<Peer> weak(
                    std::static_pointer_cast<Peer>(peer.second));
                mApp.postOnMainThread(
                    [smsg, weak, log = !broadcasted]() {
                        auto strong = weak.lock();
                        if (strong)
                        {
                            strong->sendMessage(smsg, log);
                        }
                    },
                    fmt::format(FMT_STRING("broadcast to {}"),
                                peer.second->toString()));
            }
            broadcasted = true;
        }
    }
    CLOG_TRACE(Overlay, "broadcast {} told {}", hexAbbrev(index),
               peersTold.size());
    return broadcasted;
}

std::set<Peer::pointer>
Floodgate::getPeersKnows(Hash const& h)
{
    std::set<Peer::pointer> res;
    auto record = mFloodMap.find(h);
    if (record != mFloodMap.end())
    {
        auto& ids = record->second->mPeersTold;
        auto const& peers = mApp.getOverlayManager().getAuthenticatedPeers();
        for (auto& p : peers)
        {
            if (ids.find(p.second->toString()) != ids.end())
            {
                res.insert(p.second);
            }
        }
    }
    return res;
}

void
Floodgate::shutdown()
{
    mShuttingDown = true;
    mFloodMap.clear();
}

void
Floodgate::forgetRecord(Hash const& h)
{
    mFloodMap.erase(h);
}
}
