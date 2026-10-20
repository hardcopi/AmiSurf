# AmiSurf ARexx port and single instance (0.85)

AmiSurf 0.85 and newer (all three binaries: AmiSurf, AmiSurf-020, AmiSurf-gadtools) open a public message port named
**`AMISURF`** (upper case) as soon as they start, before the window opens. Only one AmiSurf owns it.

## Commands

| Command | What it does | Result (`options results`) |
|---|---|---|
| `OPENURL "<url>"` | opens the URL in the current tab and brings AmiSurf to the front | – |
| `OPENURL "<url>" NEWTAB` | opens the URL in a new tab (it becomes the shown tab) and brings AmiSurf to the front | – |
| `FRONT` | brings the AmiSurf window (and its screen, if it has its own) to the front and activates it; un-iconifies the MUI version | – |
| `GETURL` | – | the URL of the shown tab |

* Command words and `NEWTAB` are case-insensitive. The URL may be in double or single quotes (needed when it has
  spaces); a bare word works too. Text typed in the address bar works as well (`OPENURL amiga.org` → https://amiga.org/).
* `rc` is 0 on success, 10 for an unknown command or `OPENURL` without a URL, 20 for a malformed message.
* If AmiSurf is not connected to the Host yet, `OPENURL` remembers the URL and opens it when the connection is up.
* The MUI binaries also have MUI's own port `AMISURF.1` (QUIT, SHOW, HIDE, ...). Use `AMISURF` for the commands above.

```rexx
/* open a link in AmiSurf, starting it if needed */
options results
if ~show('ports','AMISURF') then do
  address command 'Run >NIL: AmiStudio:AmiSurf/AmiSurf'
  address command 'SYS:Rexxc/WaitForPort AMISURF'
end
address AMISURF
'OPENURL "https://www.amiga.org/" NEWTAB'
'GETURL'
say result
```

## Single instance: just start AmiSurf with the URL

A second `AmiSurf <url>` (Shell, `Execute`, `SystemTags`, a Workbench project/tool start) finds the `AMISURF` port,
sends `OPENURL "<url>" NEWTAB` (or `FRONT` without a URL), waits for the answer and quits with rc 0. The running copy
opens the URL in a new tab and comes to the front. RexxMast does **not** need to be running for this
(rexxsyslib.library is enough; it is in LIBS: on every OS 2.04+ system).

So AmiDiscord (`BROWSER=` / the Host OPENURL path) and the launcher need no change: they keep starting
`AmiSurf <url>`. A launcher that wants to avoid loading a second copy at all can test `FindPort("AMISURF")` and send the
command itself (see below), or use `rx` / `WaitForPort` as above.

* If the running AmiSurf does not answer within ~10 s (it is stuck), the second copy prints
  `AmiSurf is already running but does not answer (port AMISURF)` and quits with rc 0 (it does not start a second window).
* A double quote inside the URL is dropped on the hand-over (URLs from the Host never contain one unescaped).

## Sending from C (no RexxMast needed)

```c
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
/* returns the rc from AmiSurf, or -1 when no AmiSurf runs */
LONG amisurf_cmd(const char *cmd){
  struct MsgPort *rp=CreateMsgPort(), *p; struct RexxMsg *rm; LONG rc=-1;
  if(!rp) return -1;
  rm=CreateRexxMsg(rp,NULL,NULL);
  if(rm){ rm->rm_Action=RXCOMM; rm->rm_Args[0]=(STRPTR)CreateArgstring((STRPTR)cmd,strlen(cmd)); }
  Forbid(); p=FindPort((STRPTR)"AMISURF"); if(p && rm && rm->rm_Args[0]) PutMsg(p,(struct Message*)rm); Permit();
  if(p && rm && rm->rm_Args[0]){ WaitPort(rp); GetMsg(rp); rc=rm->rm_Result1; }
  if(rm){ if(rm->rm_Args[0]) DeleteArgstring((UBYTE*)rm->rm_Args[0]); DeleteRexxMsg(rm); }
  DeleteMsgPort(rp); return rc; }
/* amisurf_cmd("OPENURL \"https://example.org/\" NEWTAB"); */
```

AmiSurf accepts messages made with `CreateRexxMsg()` (it does not require `IsRexxMsg()`); `rm_Action` must be
`RXCOMM` (with `RXFF_RESULT` set, `GETURL` returns its result in `rm_Result2` as an argstring: free it with
`DeleteArgstring()`).

Source: `ui/asb_rexx.inc`. Tested in FS-UAE (OS 3.1, MUI and GadTools builds): shots/arexx-*-085-emu.png.
