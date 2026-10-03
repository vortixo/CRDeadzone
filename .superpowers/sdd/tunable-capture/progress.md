# SDD ledger — plan: plans/tunable-capture.md
Task 1: complete (tests: full suite -> ALL 198 CHECKS PASSED)
Pre-flight: Task1 produces TunableCapture.h API; Task2 consumes IsWatchedTunable/TunableCapture/ClusterCallTargets as specced - no drift
Ruling: implement on main, no worktree - session norm is main-based pushes with CI gate; cost if wrong: main noise; mitigated by review + green CI
Finding: registrar/getter prologues match 10/147 functions in build 25600401 - no signature hook; clustering approach stands
