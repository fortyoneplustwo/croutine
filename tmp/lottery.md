# Lottery scheduling

## How it works

Tickets represent your share of the resource (CPU)

- Hold lottery every time slice:
  Pick a random number [0, n], n = total amt of tickets
- A holds 75% i.e. tickets 0 to 74
- B holds 25% i.e. tickets 76-99
- Winner gets to run next

### Advantage: Very little state to maintain. Algorithm is simple and fast.

## Implementation

- Pick a random winning number W
- Initialize counter to 0
- Walk through the run queue
- counter += curr.tickets
- If counter > W, we have a winner

To keep iterations low, best to sort queue from highest to lowest

## Fairness Metric (R)

R = time 1st job finishes / time 2nd job finishes.
We want F to be close to 1 i.e. for both jobs to finish roughly at the same time.

Because this is a probalisitic algorithm, R approaches 1 when there are lots
of job slices to run for each job i.e. job length is high.

## Problem: How to assign tickets?

Remains an open problem

# Stride scheduling: fairer results on a short time scale

It creates a repeating pattern of jobs ordering proportionate to their ticket
allocation. (Think of the repeating pattern created by the output of fizz-buzz).

## How it works

Each thread gets a

- stride: a large number / tickets owned
- pass: a value that is updated (pass += stride) everytime the job is run

At each step we pick the job with the lowest pass then update it.

## Problem: What `pass` to we assign to a new job entering the queue?

If 0, then it might monopolize the system.

Lottery scheduling doesn't need to rely on global state like pass. We add a new
job with whatever tickets it has and update the total amount of tickets (N)

# Linux Completely Fair Scheduler (CFS)

Achieves fairness AND efficiency. It spends very little time making decisions.

## How it works

### Which job to pick next?

Each job has `vruntime` which increases with physical runtime as the job is run.
The scheduler will pick the job with the lowest `vruntime`

#### Problem: Monopolization of the CPU by jobs who have woken up

Jobs that sleep for a long time may wake up with a significantly lower
`vruntime` than the rest and thus monopolize the CPU.

To prevent this, awoken jobs have their `vruntime` set to the minimum
among all other jobs.

### How long to run a job?

`sched_latency`(S): constant used to dynamically calculate time slice of a job
by dividing it by the number of jobs (n). Ensures that over S, scheduling will
be completely fair

#### Problem: Too many problems lead to a too small time slice

`min_granularity`: Constant. Never set the time slice to less than that.
As a result, scheduling won't be fair over S, but close enough.

## Providing some control over priority through Weighting (niceness)

The nicer you are, the less priority you have.

CFS maps the nice value of each process to a weight (w) through a table.

Use weights to calculate which proportion of `sched_latency` the job will get.

Tt = (w_k / [0..n]sum(w_k)) * `sched_latency`

`vruntime` is also adjusted to account for weights

`vruntime` += (w_0 / w_k) * runtime_k

## Find the next job efficiently by using Red Black Trees

Keeps most operations in O(log n). With 1000s of processes, this is noticeably
faster than O(n).
