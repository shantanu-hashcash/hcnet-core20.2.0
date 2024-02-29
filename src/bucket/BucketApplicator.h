#pragma once

// Copyright 2015 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "bucket/Bucket.h"
#include "bucket/BucketInputIterator.h"
#include "util/Timer.h"
#include "util/XDRStream.h"
#include <memory>

namespace hcnet
{

class Application;

// Class that represents a single apply-bucket-to-database operation in
// progress. Used during history catchup to split up the task of applying
// bucket into scheduler-friendly, bite-sized pieces.

class BucketApplicator
{
    Application& mApp;
    uint32_t mMaxProtocolVersion;
    uint32_t mMinProtocolVersionSeen;
    uint32_t mLevel;
    BucketInputIterator mBucketIter;
    size_t mCount{0};
    std::function<bool(LedgerEntryType)> mEntryTypeFilter;

  public:
    class Counters
    {
        struct CounterEntry
        {
            uint64_t numUpserted;
            uint64_t numDeleted;
        };

        // We avoid using medida metrics here here because BucketApplicator
        // activity is typically a rare thing that happens only during catchup
        // and we don't want to pollute the regular operational metrics
        // interface with these counts.
        VirtualClock::time_point mStarted;
        std::map<LedgerEntryType, CounterEntry> mCounters;

        void getRates(VirtualClock::time_point now,
                      std::map<LedgerEntryType, CounterEntry>& sec_count,
                      uint64_t& T_sec, uint64_t& total);

        std::string
        logStr(uint64_t total, uint64_t level, std::string const& bucketName,
               std::map<LedgerEntryType, CounterEntry> const& counters);

      public:
        Counters(VirtualClock::time_point now);
        void reset(VirtualClock::time_point now);
        void mark(BucketEntry const& e);
        void logInfo(std::string const& bucketName, uint32_t level,
                     VirtualClock::time_point now);
        void logDebug(std::string const& bucketName, uint32_t level,
                      VirtualClock::time_point now);
    };

    BucketApplicator(Application& app, uint32_t maxProtocolVersion,
                     uint32_t minProtocolVersionSeen, uint32_t level,
                     std::shared_ptr<Bucket const> bucket,
                     std::function<bool(LedgerEntryType)> filter);
    operator bool() const;
    size_t advance(Counters& counters);

    size_t pos();
    size_t size() const;
};
}
