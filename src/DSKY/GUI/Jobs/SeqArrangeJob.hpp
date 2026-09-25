///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "Job.hpp"

namespace Luminary
{
class Model;

class SeqArrange;
class DynamicPrintConfig;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

class SeqArrangeJob : public Job
{
public:
    explicit SeqArrangeJob(const Model &model, const DynamicPrintConfig &config, bool current_bed_only);
    virtual void process(Ctl &ctl) override;
    virtual void finalize(bool /*canceled*/, std::exception_ptr &) override;

private:
    std::unique_ptr<SeqArrange> m_seq_arrange;
};

} // namespace DSKY
