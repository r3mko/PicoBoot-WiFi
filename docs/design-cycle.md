# The design cycle

How a project goes from idea to `main`. There are five steps, and each ends at a gate: the next step starts only after an explicit approval. Answering a question is not an approval.

## 1. Research and design

- **Research first,** in `docs/research/`: what we already run, what exists outside, and whether to build or reuse.
- **The design,** in `docs/architecture.md`, written as numbered drafts (v0.1, v0.2, and so on). Every decision gets a number and is one of three kinds:
  - a decision by the project owner;
  - a proposal, waiting for approval;
  - an open question.
- **Each round:** the owner answers questions or corrects the draft, and the next version follows. The Status row says what changed in each version and why.
- **After every change:**
  - the PDF export is regenerated;
  - a commit, when the owner asks for one.
- **The step ends** when the design is approved, proposals included.

## 2. Implementation plan

The plan lives in `docs/implementation-plan.md`.

- It splits the design's phases into steps, each with a "done when" check.
- It says which model does what. For example:
  - one model builds the core and reviews;
  - subagents build independent parts, each in its own worktree;
  - a separate model does the security review.
- It lists what is needed from other people, such as servers, DNS, accounts and tokens.
- When the plan hits something the design doesn't settle, it lists "design points for approval". Approved points go into the design as new numbered decisions.
- It starts as a draft and becomes "Approved v1.0". Later changes get a new version (v1.1, v1.2, and so on).

## 3. Code

- **Branches:** each phase gets its own branch (`phase-1-core`, and so on). `main` holds only reviewed work.
- **Commits:** once the plan is approved, each step is committed on its branch without asking each time.
- **The end of a phase:** a working, tested increment, with live checks. The design and the plan are then updated to match what was actually built.

## 4. Code review

- Reviewers check the branch for correctness, security and fit with the design. The fixes go on the same branch.
- A separate security review comes before a service is opened up to a wider network.

## 5. Merge into `main`

After the owner's review:
- the branch is merged with a merge commit;
- the status lines in the docs say "merged";
- the PDF export is regenerated.

## After the first release

Every later change, such as a new feature or a fix after a live test, goes through the same five steps at a smaller scale:
1. research, if the change needs it;
2. a plan draft (for example "Plan v1.4 draft"), merged once approved;
3. a branch with the code;
4. the design and the plan updated to match what was built;
5. the code review's fixes;
6. the status lines;
7. the merge.
