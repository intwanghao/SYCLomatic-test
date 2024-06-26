/*
Modifications Copyright (C) 2023 Intel Corporation

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


SPDX-License-Identifier: BSD-3-Clause
*/

#include "MC_Fast_Timer.hh"
#include <vector>
#include "MonteCarlo.hh"
#include "MC_Processor_Info.hh"
#include "Globals.hh"
#include "portability.hh"
#include <cmath>

const char *mc_fast_timer_names[MC_Fast_Timer::Num_Timers] =
    {
        "main",
        "cycleInit",
        "cycleTracking",
        "cycleTracking_Kernel",
        "cycleTracking_MPI",
        "cycleTracking_Test_Done",
        "cycleFinalize"};

static double mc_std_dev(uint64_t const data[], int const nelm);

static double mc_std_dev(uint64_t const data[], int const nelm)
{
    uint64_t mean = 0.0, sum_deviation = 0.0;

    for (int ndx = 0; ndx < nelm; ++ndx)
    {
        mean += data[ndx];
    }
    mean = mean / nelm;
    for (int ndx = 0; ndx < nelm; ++ndx)
    {
        sum_deviation += (data[ndx] - mean) * (data[ndx] - mean);
    }
    return sqrt((double)sum_deviation / nelm);
}

void MC_Fast_Timer_Container::Print_Last_Cycle_Heading(int mpi_rank)
{
#ifdef DISABLE_TIMERS
    return;
#endif

    if (mpi_rank == 0)
    {
        fprintf(stdout, "\n%-25s %12s %12s %12s %12s %12s %12s\n", "Timer", "Last Cycle", "Last Cycle", "Last Cycle", "Last Cycle", "Last Cycle", "Last Cycle");
        fprintf(stdout, "%-25s %12s %12s %12s %12s %12s %12s\n", "Name", "number", "microSecs", "microSecs", "microSecs", "microSecs", "Efficiency");
        fprintf(stdout, "%-25s %12s %12s %12s %12s %12s %12s\n", "", "of calls", "min", "avg", "max", "stddev", "Rating");
    }
}

void MC_Fast_Timer_Container::Print_Cumulative_Heading(int mpi_rank)
{
#ifdef DISABLE_TIMERS
    return;
#endif
    if (mpi_rank == 0)
    {
        fprintf(stdout, "\n%-25s %12s %12s %12s %12s %12s %12s\n", "Timer", "Cumulative", "Cumulative", "Cumulative", "Cumulative", "Cumulative", "Cumulative");
        fprintf(stdout, "%-25s %12s %12s %12s %12s %12s %12s\n", "Name", "number", "microSecs", "microSecs", "microSecs", "microSecs", "Efficiency");
        fprintf(stdout, "%-25s %12s %12s %12s %12s %12s %12s\n", "", "of calls", "min", "avg", "max", "stddev", "Rating");
    }
}

void MC_Fast_Timer_Container::Cumulative_Report(int mpi_rank, int num_ranks, MPI_Comm comm_world, uint64_t numSegments)
{
#ifdef DISABLE_TIMERS
    return;
#endif

    fflush(stdout);
    mpiBarrier(comm_world);

    std::vector<uint64_t> cumulativeClock(MC_Fast_Timer::Num_Timers);
    std::vector<uint64_t> max_clock(MC_Fast_Timer::Num_Timers);
    std::vector<uint64_t> min_clock(MC_Fast_Timer::Num_Timers);
    std::vector<uint64_t> sum_clock(MC_Fast_Timer::Num_Timers);
    std::vector<uint64_t> std_dev_use(num_ranks); // used to calculate standard deviation

    for (int timer_index = 0; timer_index < MC_Fast_Timer::Num_Timers; timer_index++)
    {
        cumulativeClock[timer_index] = this->timers[timer_index].cumulativeClock;
    }

    mpiReduce(&cumulativeClock[0], &max_clock[0], MC_Fast_Timer::Num_Timers, MPI_UINT64_T, MPI_MAX, 0, comm_world);
    mpiReduce(&cumulativeClock[0], &min_clock[0], MC_Fast_Timer::Num_Timers, MPI_UINT64_T, MPI_MIN, 0, comm_world);
    mpiReduce(&cumulativeClock[0], &sum_clock[0], MC_Fast_Timer::Num_Timers, MPI_UINT64_T, MPI_SUM, 0, comm_world);

    this->Print_Cumulative_Heading(mpi_rank);

    for (int timer_index = 0; timer_index < MC_Fast_Timer::Num_Timers; timer_index++)
    {
        mpiGather(&cumulativeClock[timer_index], 1, MPI_UINT64_T, &std_dev_use[0], 1, MPI_UINT64_T, 0, comm_world);

        uint64_t ave_clock = sum_clock[timer_index] / num_ranks;
        if (mpi_rank == 0)
        {
            fprintf(stdout, "%-25s %12lu %12.3e %12.3e %12.3e %12.3e %12.2f\n",
                    mc_fast_timer_names[timer_index],
                    (unsigned long)this->timers[timer_index].numCalls,
                    (double)min_clock[timer_index],
                    (double)ave_clock,
                    (double)max_clock[timer_index],
                    (double)mc_std_dev(&std_dev_use[0], num_ranks),
                    (100.0 * ave_clock) / (max_clock[timer_index] + 1.0e-80));
        }
    }
    if (mpi_rank == 0)
    {
        int cycleTracking_Index = 2;
        fprintf(stdout, "%-25s %12.2f %-25s\n",
                "Figure Of Merit",
                (numSegments / (max_clock[cycleTracking_Index] * 1e-0)), // updated from segments to mega segments
                "[Num Mega Segments / Cycle Tracking Time]");
    }
}

void MC_Fast_Timer_Container::Last_Cycle_Report(int report_time, int mpi_rank, int num_ranks, MPI_Comm comm_world)
{
#ifdef DISABLE_TIMERS
    return;
#endif

    if (report_time == 1)
    {
        fflush(stdout);
        mpiBarrier(comm_world);

        std::vector<uint64_t> lastCycleClock(MC_Fast_Timer::Num_Timers);
        std::vector<uint64_t> max_clock(MC_Fast_Timer::Num_Timers);
        std::vector<uint64_t> min_clock(MC_Fast_Timer::Num_Timers);
        std::vector<uint64_t> sum_clock(MC_Fast_Timer::Num_Timers);
        std::vector<uint64_t> std_dev_use(num_ranks); // used to calculate standard deviation

        for (int timer_index = 0; timer_index < MC_Fast_Timer::Num_Timers; timer_index++)
        {
            lastCycleClock[timer_index] = this->timers[timer_index].lastCycleClock;
        }

        mpiReduce(&lastCycleClock[0], &max_clock[0], MC_Fast_Timer::Num_Timers, MPI_UINT64_T, MPI_MAX, 0, comm_world);
        mpiReduce(&lastCycleClock[0], &min_clock[0], MC_Fast_Timer::Num_Timers, MPI_UINT64_T, MPI_MIN, 0, comm_world);
        mpiReduce(&lastCycleClock[0], &sum_clock[0], MC_Fast_Timer::Num_Timers, MPI_UINT64_T, MPI_SUM, 0, comm_world);

        this->Print_Last_Cycle_Heading(mpi_rank);

        for (int timer_index = 0; timer_index < MC_Fast_Timer::Num_Timers; timer_index++)
        {
            mpiGather(&lastCycleClock[timer_index], 1, MPI_UINT64_T, &std_dev_use[0], 1, MPI_UINT64_T, 0, comm_world);

            uint64_t ave_clock = sum_clock[timer_index] / num_ranks;
            if (mpi_rank == 0)
            {
                fprintf(stdout, "%-25s %12lu %12.3e %12.3e %12.3e %12.3e %12.2f\n",
                        mc_fast_timer_names[timer_index],
                        (unsigned long)this->timers[timer_index].numCalls,
                        (double)min_clock[timer_index],
                        (double)ave_clock,
                        (double)max_clock[timer_index],
                        (double)mc_std_dev(&std_dev_use[0], num_ranks),
                        (100.0 * ave_clock) / (max_clock[timer_index] + 1.0e-80));
            }
        }
    }
    Clear_Last_Cycle_Timers();
}

void MC_Fast_Timer_Container::Clear_Last_Cycle_Timers()
{
    for (int timer_index = 0; timer_index < MC_Fast_Timer::Num_Timers; timer_index++)
    {
        this->timers[timer_index].lastCycleClock = 0;
    }
}
