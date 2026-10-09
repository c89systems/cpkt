# Review without growing a framework

Review the whole affected contract, not just the last patch. Correctness,
determinism, safety and simplicity are joint acceptance criteria.

For each finding:
1. Identify a concrete failure in a declared workflow and the evidence for it.
2. Check the operating assumptions in SKILL.md and the owning project spec.
3. For a supported failure, fix the smallest cause and add a focused regression.
4. For a violated precondition or undeclared feature, clarify the contract or
   add a local explanatory comment. Do not implement speculative support.
5. Reject a proposal that adds more mechanisms than the supported problem needs.
   Prefer native tools, shared prerequisites and removing duplicate behavior.

A review finding is not automatically an instruction to code. Tool familiarity,
short individual patches and a passing test do not prove the overall result is
simple. New persistent state, interpreters, supervisors or generalized services
need an explicit product requirement; they are not ordinary remediation.

Reviewers must also protect real boundaries: checksum failures, wrong artifact or
dependency identity, required tests not passing, unsafe deletion and unapproved
publication remain defects. Calling these unsupported does not remove the gate.

Examples of appropriate dispositions:

| Concern | Disposition |
| --- | --- |
| Only a wrapper PID is signalled while a build runs | Caller cancels the owned job/group; document that interface. No helper supervisor. |
| Synthetic Git tests inherit arbitrary account hooks | Do not create Git repositories/commits merely to validate this guide. A project that needs Git fixtures chooses an explicit test environment. |
| Code changes during a test or mtimes are deliberately preserved | Restore stable ownership and invalidate affected evidence; no concurrent-edit detector or global fingerprint service. |
| A verified immutable tool cache is edited by another process | Stop and reprepare under its owner's contract; no per-file tamper attestation. |
| Cache pathname contains ordinary text such as status=ready | Parse the actual status field; data must not supply success. |
| Selected dependency or shipped payload is missing/wrong | Fail clearly and test the actual contract; do not waive it. |

After each coherent repair batch, verify and commit before re-reviewing.
Do not rerun broad suites to debug a finding. End when no actionable supported
issues remain and a final simplicity sweep finds no unnecessary machinery.
