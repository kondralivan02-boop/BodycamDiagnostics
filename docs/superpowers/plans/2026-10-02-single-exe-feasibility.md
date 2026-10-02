# Single EXE feasibility checkpoint

The runtime currently executes two independently pinned tools: PresentMon.exe and ffmpeg.exe. The portable release checks their SHA-256 before execution. A true single-file *download* can embed these binaries as resources, but execution still requires extracting them to disk because the current controllers launch paths through CreateProcessW.

For a separate experimental artifact, CI would need to download the exact manifest-pinned assets, verify both hashes, embed them as resources, extract to a per-user directory, verify each extracted hash, then remove temporary copies at normal shutdown. An old Gizmo may flag this extraction or subsequent process launch. The club PC must validate compatibility after Gizmo is updated; a CI build alone cannot establish that.

Decision for this branch: ship the transparent portable package with verified tools and Russian instructions. Do not label an extracting executable as conflict-free or as containing no other executables at runtime. Revisit the experimental artifact after administrator testing and tool distribution/licensing review.
