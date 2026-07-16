# Problem

- Optimize Tt
- Minimize Tr

Can this be done with a priori knowledge?

# Basic idea

-Multiple queues, each with a different priority level

- Priorities of jobs will vary based on observed behaviour

# Basic rules

1. Run the job with the highest priority
2. If tied, run both in Round Robin
3. When a job enters the system, it is placed at the highest priority.
   • Rule 4a: If a job uses up its all
4. (a) If a job uses up its allotment while running, it moved down a queue
   (b) If a job gives up the CPU before the allotment is up, it stays at the
   same priority level.

> **Allotment**: amount of time a job can spend at a given prop

# Approximates SJF

- Assumes any new job might be short
- If it happens not to be a short job, it moved down in priority
- Otherwise it keeps the same priority (P)

# Handles I/O tasks well

Interactive tasks will relinquish the CPU before allotment is over,
so they will be reset and keep same P

# Problems

- Starvation: too many interactive jobs can starve long-running tasks
  before allotment is up.
- Programs change behaviour with time. A long-running task could become
  interactive. This is not reflected in our current system.
- User can write its program to game the scheduler: just issue an I/O op

# Solution: priority boost

5. After some time period (S), move all jobs to the top queue.

This solves the first two problems.

# Better accounting

4. Keep track of how much of its allotment a process has used up. Once used up,
   it gets demoted, even if it is interactive.

This prevents gaming of the scheduler.
