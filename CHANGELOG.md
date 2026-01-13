# Changelog

All notable changes to erssi will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.6] - 2026-01-13

### Wip

- **fe-notcurses**: Experimental notcurses backend - blocked by tmux/screen support

### ⚡ Features

- **fe-notcurses**: Add image preview support for IRC URLs
- **fe-notcurses**: Add og:image extraction for page URLs
- **fe-notcurses**: Add horizontal separator above statusbar
- **fe-ansi**: Add ANSI backend and refactor fe-notcurses
- **image-preview**: Add inline image preview with wide terminal support
- **image-preview**: Click-only URL detection with retry and error handling

### 🐛 Bug Fixes

- **sidepanels**: Clear cache when right panel destroyed to fix empty nicklist bug
- **term**: Prevent sidepanel boundary violations during scroll/clear
- **resize**: Skip resize when only pixel dimensions change
- **fe-notcurses**: Add resize support via notcurses_refresh
- **sidepanels**: Guard terminfo borders for notcurses compatibility
- **sidepanels**: Clear cache when right panel destroyed to fix empty nicklist bug
- **fe-notcurses**: Flush terminal responses and fix border color
- **resize**: Prevent UI freeze when terminal shrinks below sidepanel space
- **term**: Prevent sidepanel boundary violations during scroll/clear
- **resize**: Skip resize when only pixel dimensions change
- **image-preview**: Proper popup clearing and line tracking
- **image-preview**: Use Safari User-Agent to bypass bot detection
- **ci**: Fix release workflow dependencies

### 📚 Documentation

- Update CHANGELOG.md for v1.2.5 [skip ci]

### 🔨 Miscellaneous

- Remove unused detect_passthrough function
- **release**: V1.2.6

## [1.2.5] - 2025-12-14

### 📚 Documentation

- Update CHANGELOG.md for v1.2.4 [skip ci]

### 🚀 Performance

- **rendering**: Add freeze/thaw buffering to dirty_check()

## [1.2.4] - 2025-12-14

### ⚡ Features

- **sidepanels**: Implement differential rendering to eliminate flicker

### 🐛 Bug Fixes

- **sidepanels**: Clear all unused lines to prevent tmux artifacts

### 📚 Documentation

- Update CHANGELOG.md for v1.2.3 [skip ci]
- **readme**: Update Latest Release section to v1.2.4
- Update CHANGELOG.md for v1.2.4 [skip ci]

## [1.2.3] - 2025-12-13

### 🐛 Bug Fixes

- **sidepanels**: Prevent system messages from triggering activity
- **irc**: Detect first run by config directory existence
- **irc**: Check config file instead of directory for first run

## [1.2.2] - 2025-11-30

### 📚 Documentation

- Update CHANGELOG.md for v1.2.1 [skip ci]

### 🔨 Miscellaneous

- Remove CLAUDE.md from repository

## [1.2.1] - 2025-11-29

### Fe-netsplit

- Fix nickname truncation to avoid trailing comma-space

### 🐛 Bug Fixes

- **fe-web**: Reinitialize crypto on fe_web_enabled to pick up password changes
- **formats**: Fix hide_text_style and hide_colors to mitigate color bleed

### 📚 Documentation

- Streamline README with professional release structure
- Update CHANGELOG.md for v1.2.0 [skip ci]
- Remove outdated Version History section from README

## [1.2.0] - 2025-11-18

### 🐛 Bug Fixes

- Update version strings in startup banner and /version command to v1.1.0
- **sidepanels**: Resolve batching conflicts and optimize redraw timing
- **ci**: Checkout main branch before pushing CHANGELOG.md
- **ci**: Use authenticated remote and explicit branch creation
- **ci**: Stay in detached HEAD and push using HEAD:refs/heads/main
- **ci**: Use separate checkout step for changelog update
- **ci**: Force create local main branch with git checkout -B
- **ci**: Use git-auto-commit-action for changelog push
- **ci**: Add branch parameter to git-auto-commit-action
- **ci**: Add explicit branch setup step before git-auto-commit
- **ci**: Remove git fetch that conflicts with checked out branch

### 📚 Documentation

- Update version number in README.md to v1.1.0
- Add v1.1.0 entry to CHANGELOG.md

### 🔧 Build System

- Enable automatic CHANGELOG.md generation in release workflow

## [1.1.0] - 2025-11-08

### Fix

- Send query windows in state_dump on reconnect - 2025-10-22 13:01:36

### Fe-web

- Remove automatic window switching from C code - now handled in Node.js layer

### 📚 Documentation

- Add CLAUDE.md - AI assistant guidance for erssi development

## [1.0.0] - 2025-10-18

### /connect

- Add help for -nocap

### /server

- Add help for -cap/-nocap

### Lang-Fix

- Use more general messages

### Core

- Remove unused len variable

### Fe-text

- Include the real tputs(3) from term.h
- Remove const for better Darwin support

### ⚡ Features

- Update startup banner to erssi branding
- Add credential frontend commands (/credential)

### 🐛 Bug Fixes

- Fix yet another meson regression

broken by meson 0.60.0

- Fix scrollback redraw not updating linecount

- Fix memory leak when updating single line

- Fix visible lines expiring from cache

- Fix recount of lines after window set

- Fix recount of lines after scroll

- Fix crash when loading server without chatnet

- Fix clang-format

- Fix lastlog -window

- Fix signals file

- Fix crash on connect in startup

fixes #1449

does not completely fix the issue (connect still does not work, error message is misleading)

- Fix stale special collector use after free

reported by ednash and investigated by @dwfreed

- Fix usage of $type in ExtUtils::ParseXS 3.50

- Fix /ban command

- Fix irssi SASL negotiation with multiple CAP ACK

when attempting SASL, if multiple CAP ACK were received (for example,
because a script sent an additional CAP REQ), then any not containing
`sasl` would cause SASL to fail (immediately aborting the connection
with no message, depending on sasl_disconnect_on_failure setting)

check not only if SASL is set in this line, but also if we've already
seen it in a previous line.

- Fix _GNU_SOURCE redefined warning due to perl ccflags

- Fix xbps

- Add fe-web compilation flag to install-erssi.sh
- Correct fe-web compilation flag in install-erssi.sh
- Change project name from 'irssi' to 'erssi' in meson.build

### 📚 Documentation

- Document meson apple workaround

workaround for https://github.com/mesonbuild/meson/issues/11165


### 🔨 Miscellaneous

- Update for github workflows deprecations

### 🧪 Testing

- Test solaris build with vmactions

- Test the void


## [1.5+1-dev] - 2022-06-06

### NEWS

- Remove 2 lines that would be confusing

### Fe-text/mainwindows

- Fix /window balance warning

### Hilight.in

- Fix typo the->to

### Meson.build

- Remove unnecessary -Wall

### ♻️ Refactoring

- Refactor glib install

- Refactor quit message into a separate function


### 🎨 Styling

- Style format change


### 🐛 Bug Fixes

- Fix test on Big Endian 64bit, due to pointer size mismatch

- Fix missing AC_DEFINE

- Fix irssi-version link

- Fix reconnect of multiplexed proxy

- Fix realpath on old solaris

POSIX.1-2001 did not implement realpath(..., NULL) yet.
Fixes #1042

- Fix code block NEWS formatting

- Fix small overflow

- Fix cut off text with theme-indent and /set indent_always off

- Fix build system debug config to include -fno-omit-frame-pointer

- Fix glib version dependency

- Fix cap queue order

- Fix crash in setname

- Fix crashes when nick is missing

- Fix autotools build

- Fix wrong version

- Fix crash on startup when resizing before active_win

- Fix use after free receiving caps

fixes GL#34

- Fix warning in fe-fuzz/server-fuzz

- Fix crash in join due to incorrect free

- Fix the fix

- Fix #641

Track the address family of the last failed connection attempt
(either immediately or during TLS handshake), then disprefer
that address family during reconnection.

- Fix /ignore ... MODES NO_ACT not working

reported by letty

- Fix perl module build on openbsd

unfortunately, some mangling is needed to create the correct linker
and compiler invocations

- Fix crash on /quit when unloading modules

this fixes a crash on /quit when the module unloaded is trying
to reference symbols from already-unloaded modules, by reversing
the lists.

- Fix npe on no text from format_get_text_theme_charargs

- Fix NULL assertion in format args

- Fix extended bg colours

- Fix crash when server got disconnected/reconnected before it was properly connected

- Fix missing wrapping of line in signals

- Fix clang-format-xs option parser by using getopt

- Fix clang-format-xs formatting whole file

regression of #1230

- Fix clang-format-xs formatting whole file

incomplete fix in #1234

- Fix multiple identical active caps

- Fix autotools build

- Fix assertion failure when the line does not have text (yet)

- Fix level uninitialised

Credit to OSS-Fuzz

- Fix clang formatting

- Fix missing output

- Fix off by one

- Fix double free

- Fix crash on /connect /dev/null

- Fix crash on /connect -tls

fixes #1239

- Fix crash on tls error

- Fix reconnect to use tls settings

- Fix fe-fuzz

- Fix build with meson 0.58.0

- Fix use of wrong "equal" function in meta hash tables

- Fix wrong server_time in $line->get_meta

- Fix recursive crash in Perl scripts

- Fix reading of old config ssl_verify key

- Fix reading of starttls = "no" in config

- Fix reading of starttls = "no" in config, attempt 2

- Fix queue bug

- Fix pedantic error in MSGLEVEL enum

reported by horgh

- Fix /server modify port argument order

- Fix wrong argument type in printformat

- Fix max_lag disconnect

- Fix stuck meta on dcc chat

- Fix help text wrt SERVER command

reported by Remco

- Fix help text wrt SERVER LIST command

- Fix crash in Perl's $view->set_bookmark

reported on the freebsd issue tracker https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=254237#c13

- Fix crash when accessing settings during shutdown

- Fix formatting

- Fix intentation

- Fix some stupidness

- Fix something

- Fix meson deprecations

- Fix irc module in fuzzer

- Fix clang-format

- Fix scroll_page_count settings with .

- Fix formatting

- Fix missing len in g_array_copy


### 🔧 Build System

- Build fixes when using install-glib and on openbsd


### 🧪 Testing

- Test termux pkg

- Test OBS workflow


## [1.3-dev] - 2019-02-11

### Channel_change_topic

- Change one strlen == 0 to *str == '\0'

### Configure

- Fix Perl detection on macOS Mojave

### Core/ignore

- Fix ignore_match_level handling of flag levels
- Fix #900

### Fe-text

- Add window_default_hidelevel setting
- Only save non-default window hidelevel
- Clear hidelevel in layout if default

### Gui-windows

- Make the wcwidth_impl global var into static

### Irc-cap

- Don't show warning on CAP LIST response

### Modules_deinit

- Fix -Werror=declaration-after-statement

### Otr

- Add KEY_GEN_STARTED state to avoid starting it twice
- Fix missing 'target' param in 'message private' signal
- Add target param to the unencrypted 'message private' signal
- Rename module.c to otr-module.c
- Fix blatant lies in help text

### Signals.txt

- Add missing 'server cap new|delete' signals

### Wcwidth-wrapper

- Avoid cast with a tiny wrapper, julia_wcwidth()

### ♻️ Refactoring

- Refactor common parts

- Refactor cnotice test into function


### 🐛 Bug Fixes

- Fix test builds on some platforms

- Fix sequence error

- Fix crash in notifylist

- Fix uaf in signal path

- Fix build with LibreSSL 2.7.0

- Fix a crash when trying to append to a NULL line

reported by @vague666

- Fix build

- Fix accessing unallocated text when checking entry position

fixes #928

- Fix compilation of theme-indent module

- Fix compilation of python module

- Fix irssi being stuck when resized very small

- Fix and document window width on screen width dependency

- Fix flood test libs

- Fix duplicate include guard

- Fixup perl side

- Fix paste_join_multiline

- Fix width of byte codepoints

- Fix backward completion

- Fix gui_input_get_extent

it was causing a free of data with [transfer=none]


### 🧪 Testing

- Test line joining


## [1.2-dev] - 2018-01-08

### Consistency

- Use FALSE instead of 0.

### NEWS

- Avoid explicitly mentioning freenode in the pinning examples

### Dcc.in

- Fixed typo 'resolved' -&gt; 'resolves'

### Expand_escape

- Expand double backslash as a backslash

### Fe-common-core

- Fix redeclaration of server_tag_len

### Fe-dcc-

- **get|send**: Fix some -Wpointer-compare with newer gcc

### Fe-netjoin

- Remove irc servers on "server disconnected" signal

### Notify-ison

- Don't send ison before the connection is done

### Parse_time_interval

- Allow negative time in settings

### Term-terminfo

- Avoid switching out of alt screen on unexpected exits

### ♻️ Refactoring

- Refactor history to use history_entries list

this allows access to the global history even when a using /window history
named or /set window_history on, and you want to recall something from one
of the other windows' histories.

usage (default): ctrl+up/down


### 🐛 Bug Fixes

- Fix regression in completion

fixes #609

- Fix dcc get

fixes #656

- Fix weird n-fold unescaping

- Fix out of bounds read in compress_colors

Reported by Hanno Böck.

Fixes GL#12

- Fix uaf in chanquery module

the chanquery needs to be removed in any case if a channel rec is
destroyed, regardless of any state

Fixes GL#13

- Fix dcc issue

- Fix key length checker to actually do some work

- Fix comments

- Fix /exec -o for blank lines

since it is not allowed to send nothing, instead of spamming the status window
with error, send " " instead

Fixes FS#902

- Fix some more

- Fix redraw


### 🧪 Testing

- Test trusty container


## [1.1-dev] - 2017-01-05

### /IGNORE

- -word -> -full, like it's with /HILIGHT.

### /LASTLOG

- Start parameter wasn't handled correctly

### /LIST

- Don't require -yes option if there's 1000 channels or less.

### /LOAD

- When using '.' character in module name irssi printed glib error

### /NICK

- Don't bother trying to change the nick to the one you already have

### /UPGRADE

- --home and --config parameters weren't passed to new irssi.

### AUTHORS

- Move myself from contributors to staff

### Bugfix

- Http://bugs.irssi.org/?do=details&id=99
- Http://bugs.irssi.org/?do=details&id=121

### INSTALL

- Mention local::lib for home directory installations

### Irssi

- :signal_emit(): changed max. parameter count from 6 to 7
- :signal_emit() was broken.
- :printformat() didn't work
- :command_runsub() - patch by fuchs
- :xx -> Irssi::UI::xx
- :Server::command() - window item parameter should be NULL
- :TextUI::TextBufferVew should also contain the scroll..
- :Chatnet can now be accessed from perl.
- :printformat() crashed if the registered format contained $0- etc.
- :ignores() wasn't working, it looked at the server list..
- :signal_add_first() and .._last() allows hashes now.
- :command_bind*() allows using hash.
- :timeout_add() - don't allow smaller values than 10
- :format_get_text() didn't work
- :Theme::get_format() now uses format tag instead of number.
- :get_gui() now returns IRSSI_GUI_xxx which is in use.
- :timeout_add(), timeout_add_once() and input_add() were buggy.

### Makefile.am

- Add default-config.h and default-theme.h to CLEANFILES

### NEWS

- Describe my changes (not others') since 0.8.11.

### README.md

- New issues should be opened on GitHub, minor grammar fixes
- Tell users to look at GitHub issues too

### Release

- 0.8.14

### SASL

- Handle fragmentation

### SYNTAX

- ACTION updated - target is required

### WINDOW_REC

- Added width and height variables

### Autolog

- Target name is now always lowercased with irc protocol.
- Change some characters illegal in Windows filenames to underscores

### Bugfix

- Nick1,nick2,nick3 -> nick1 nick2 nick3

### Completion

- Fix crash when the complist provided by a script has nulls

### Cutbuffer

- Do not unconditionally use replace when noop was requested

### Dcc-get

- Close() the temp fd so we don't get ETXTBSY in ntfs mounts

### Fe_channel_skip_prefix

- Fix return value (FALSE/NULL isn't valid)

### Get_alignment

- Handle UTF-8 strings.

### Git-svn-id

- Http://svn.irssi.org/repos/irssi/trunk@4410 dbcabf3a-b0e7-0310-adc4-f8d773084564
- Http://svn.irssi.org/repos/irssi/trunk@4421 dbcabf3a-b0e7-0310-adc4-f8d773084564

### Http

- //irssi.org -> http://irssi.org/

### Int

- 1 -> unsigned int:1

### Irc-cap

- Don't send a space at the beginning of the CAP REQ parameter

### Irc/core/irc-commands.c

- Fix indentation

### Irc_server_send_away

- Don't send empty param if there's no away reason

### Irssi.conf

- Improve /CALC alias:

### Irssiproxy

- Avoid using pointer after freeing it

### Net_gethosterror

- Handle EAI_SYSTEM ("System error") properly

### Network-openssl

- Show why a certificate failed validation.

### Paste_bracketed_end

- Fix rest length calculation

### Servers-reconnect

- Pass unix_socket attribute to new connection

### Sig_gui_print_text

- Don't crash if dest is NULL.

### Sig_message_irc_op_public

- Fix nickmode lookup, use cleantarget instead

### Ssl

- Add option to specify SSL cipher suite preference.
- Fixed call to SSL_CTX_set_cipher_list() only when ssl_ciphers specified and warn when no cipher suite could be selected.

### Strsplit_len

- Use strlen() directly instead of a remaining_len variable
- Make it look more like the original version

### Trivial

- Minor cosmetic changes fixing docs as per review from ahf.

### Typo

- Themes weren't defaulting their abstracts to internal theme

### Wallchops

- Works only with ircu. updated ircu notes.

### 🐛 Bug Fixes

- Fixed


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@174 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@175 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed a crash (first cvs commit in home for 2 months :)


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@244 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed two minor memleaks. irc/bot directory isn't now build if you specify
--without-bot parameter to configure


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@294 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@573 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@621 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix for multiserver support


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@688 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@690 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes for multiprotocol support


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@692 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes .. still not perfect


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@713 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@740 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@768 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed notices in theme.


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@802 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix to ping/pong handling :)


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@823 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed text buffer crash when scrollback got full


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@893 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@933 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes, perl should work correctly now :)


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@982 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed $topic uninit
added $winref


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@989 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed minor memory leak


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1060 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed some signed/unsigned issues


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1304 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed minor memory leak


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1350 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed small hilight memory leak


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1404 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes, hopefully works correctly finally :) patch by fuchs.


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1420 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed descriptions


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1426 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed commented out module_load()


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1433 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed potential crash


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1459 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed config file handling


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1485 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed a small memory leak


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1643 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed using already free'd memory.


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1648 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1678 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed a minor memleak


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1836 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed a small memleak when unloading module


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1843 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix a fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1869 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed server list - added a ',' ..


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1891 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed wrong hash key lengths, patch by peder@ifi.uio.no


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2224 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed module_uniq_destroy() calls


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2444 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed a compiler warning


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2500 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix to some broken "ircds"


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2589 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed several signal leaks


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2683 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes for new signaling code.


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2691 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2692 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed buffer overflow - happened at least when hitting ^A after writing
enough text to input line. usually didn't crash..


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2755 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed /WHO handling


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2762 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed memory leaks with several functions.


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2789 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2791 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed query to work with nicks beginning with '-' char


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2859 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed --disable-ssl description


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2895 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes to work with glib2 (untested...)


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2897 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix for new perls


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2979 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes to allow you to register a new keyboard redirection inside a
redirection handler. patch by c0ffee.


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@2995 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix compiler warnings


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3077 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix, try #2


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3160 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3172 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3200 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3207 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3210 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3214 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes for isupport-draft-incompatible servers sending 005 events..


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3223 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3227 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed channel->chanop


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3230 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed /BIND escape_char


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3234 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix minor textbuffer leak, Bug 288 by Toby

git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3897 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix completion for /format (Bug 143)

git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3991 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed recoding of own messages.
recode after expand_emphasis
remove the redundant call to setlocale(LC_CTYPE, )


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@4038 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed recode for actions and added recode for numeric events

git-svn-id: http://svn.irssi.org/repos/irssi/trunk@4041 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed a bug in recode when target is NULL and really use the recoded string for printing

git-svn-id: http://svn.irssi.org/repos/irssi/trunk@4042 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix segfault on /quit by using a linked list node after freeing it (by  Chris Moore)

git-svn-id: http://svn.irssi.org/repos/irssi/trunk@4202 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix off by one error when extracting e.g. fe from fe_silc

git-svn-id: http://svn.irssi.org/repos/irssi/trunk@4553 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fix print_after scrollback

- Fix segfault with xmpp query in layout

- Fix deprecated perl warnings in scriptassist by removing all occurences of "defined"

- Fix package of term_refresh_* script api

- Fix implementation of format_get_text script api

- Fix signals parser

- Fix compiler warnings in extended colour code

- Fix uninitialised copy on 24bit colours

- Fix colour 0 again

the previous commit was broken, as it conflicted with the colour
\#000000. Now both the "real colour black" and the "terminal colour 0"
are working.

- Fix rules for italics emphasis

while the last patch did stop /path/.xxx from turning italic, it also
stopped any other /emphasis/ from becoming italic. correct this by
testing for ispunct, so spaces are valid italic terminators

- Fix mirc_blink_fix

the background colours were totally off with mirc_blink_fix
enabled. oops.

reported by wodim

- Fix crash in layout code when encountering wrong config

- Fix nick class hierarchy

- Fix indentation, undelete line not meant to be deleted.

- Fix whitespace

- Fix formatting

- Fix proxy server name

- Fix race condition in terminal init

remove the tcgetattr call to a single time on irssi load instead of
querying it each time. Fixes #450

- Fix dist compilation failure

remove illegal wcwidth.c include and compile wcwidth.c
correct #include in wcwidth.c
fallout from #480

- Fix use after free in expando error

- Fix nick->host == NULL crash

- Fix %[

- Fix GRegex GError problem


### 📚 Documentation

- Docs generator updates


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@1074 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Doc/syntax updates


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@3020 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Updated help for exec, EXEC without any arguments displays the list of started processes
- Added the explanation of the -noproxy option of in the server command

### 🧪 Testing

- Test newer perls now available on travis

- Test make dist in travis


## [0.7.90-cvs] - 2000-04-14

### GtkIText

- Imlib isn't required anymore, underlined text works

### 🐛 Bug Fixes

- Fix for building irssi from different directory


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@17 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed bug in configuring popt


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@35 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed MSGLEVELS in plugins


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@58 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed some problems with ignoring server modes. Added different format
text for server modes.


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@97 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixes by vkoivula@saunalahti.fi


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@116 dbcabf3a-b0e7-0310-adc4-f8d773084564

- Fixed default configuration file


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@147 dbcabf3a-b0e7-0310-adc4-f8d773084564


### 📚 Documentation

- Docs/help - online helps for /HELP. Anyone care to write them? :)


git-svn-id: http://svn.irssi.org/repos/irssi/trunk@69 dbcabf3a-b0e7-0310-adc4-f8d773084564


<!-- generated by git-cliff -->
