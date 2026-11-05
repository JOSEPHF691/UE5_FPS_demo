# Git Bootstrap Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to execute this small repository setup in the current session.

**Goal:** Establish local version history for the complete Unreal source project, then push it to the user's selected GitHub repository.

**Architecture:** Ordinary Git stores source, configuration, scripts and documentation. Git LFS stores Unreal assets and PNG originals; generated files and known private keys stay local.

**Tech Stack:** Git for Windows, Git LFS, Unreal Engine 5.7, GitHub.

**Spec:** `docs/version-control.md`, recording the approved chat proposal.

## Global Constraints

- Preserve existing project files and the configured Git author identity.
- Retain assets under Content/RAR and SourceArt; exclude nested generated caches.
- Never stage Content/RAR/Config/DefaultCrypto.ini or disclose key values.
- Clear Android File Server SecurityToken values in both DefaultEngine.ini files, preserving the originals only under ignored Saved/GitBootstrapLocalConfig.
- Configure LFS before staging assets. Do not overwrite an existing remote history.
- Obtain the destination owner/repository before remote writes.

## Review Focus

- Known private keys must be absent from the index and commit.
- External actors, maps and referenced RAR assets must be retained.
- All staged assets covered by LFS rules must be valid LFS pointers.
- No generated caches or main-project binaries may enter the commit.
- Remote publication must use the destination supplied by the user.

## Task 1: Configure and commit the local repository

- [x] Create `.gitignore`, `.gitattributes`, and version-control documentation.
- [x] Initialize `main`, enable repository-local long paths and install local LFS hooks.
- [x] Check representative ignored and retained paths, and audit candidate text for credentials.
- [x] Stage files; verify the index, LFS pointers and non-LFS file sizes.
- [x] Obtain an independent configuration review and commit the verified source project as d8fe1eb.

## Task 2: Publish to the selected GitHub repository

- [x] Resolve the user-supplied destination and inspect existing remote history: https://github.com/JOSEPHF691/UE5_FPS_demo.git returned no refs before the first push.
- [x] Push Git commits and LFS objects without force, then verify the remote main ref and upload status.

## Verification record

- 5,855 indexed files; 5,663 valid LFS pointer files; 5,206 unique LFS objects (about 4.702 GiB).
- 296 external actors and 12 external objects retained; known private key and generated paths excluded.
- Independent configuration and candidate-text credential reviews completed before the first commit.
- Both Android File Server token cleanups were independently checked against ignored local backups; all other bytes match.
- `git lfs fsck --objects --pointers` reported `Git LFS fsck OK`; `git fsck --full --no-dangling` exited successfully.
- All 5,206 LFS objects uploaded successfully. Remote main matched the initial source commit d8fe1eb94bbec41872810b63ab6e1a20d0831e79; LFS status was empty and the working tree was clean before this documentation update.
- Scope ruling: repository initialization runs in the existing project directory; there is no prior commit or branch to isolate, and no gameplay code or asset is changed.
- Network ruling: ordinary Git and LFS lost direct connectivity to GitHub port 443. The Windows proxy was already enabled at 127.0.0.1:7890; its connectivity test passed, and using it in repository-local Git configuration allowed the retry to finish.
- Runtime tests are not needed for version-control configuration; verify index contents, LFS integrity and remote refs instead.
