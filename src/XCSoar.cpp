// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

/**
 * This is the main entry point for the application
 * @file XCSoar.cpp
 */

#include "Startup.hpp"
#include "LocalPath.hpp"
#include "Version.hpp"
#include "LogFile.hpp"
#include "CommandLine.hpp"
#include "MainWindow.hpp"
#include "Interface.hpp"
#include "Look/GlobalFonts.hpp"
#include "ui/window/Init.hpp"
#include "net/http/Init.hpp"
#include "ResourceLoader.hpp"
#include "Language/Language.hpp"
#include "Language/LanguageGlue.hpp"
#include "Simulator.hpp"
#include "Audio/GlobalPCMMixer.hpp"
#include "Audio/GlobalPCMResourcePlayer.hpp"
#include "Audio/GlobalVolumeController.hpp"
#include "system/Args.hpp"
#include "io/async/GlobalAsioThread.hpp"
#include "io/async/AsioThread.hpp"
#include "util/PrintException.hxx"

#ifdef ENABLE_SDL
/* this is necessary on macOS, to let libSDL bootstrap Quartz
   before entering our main() */
#include <SDL_main.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#import <AppKit/AppKit.h>
#endif
#endif

#include <cassert>

#if defined(KOBO) && defined(__linux__)
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#if defined(KOBO) && defined(__linux__)
static void
DumpFileToStderr(const char *path) noexcept
{
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return;

  char buffer[1024];
  ssize_t n;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0)
    (void)write(STDERR_FILENO, buffer, n);

  close(fd);
}

static void
CrashSignalHandler(int signo, siginfo_t *info, void *context) noexcept
{
  (void)write(STDERR_FILENO, "\n==== xcsoar crash ====\n", 23);
  dprintf(STDERR_FILENO, "signal=%d fault_addr=%p\n",
          signo, info != nullptr ? info->si_addr : nullptr);

#ifdef __arm__
  const auto *uc = static_cast<const ucontext_t *>(context);
  if (uc != nullptr) {
    const auto &m = uc->uc_mcontext;
    dprintf(STDERR_FILENO,
            "arm_pc=%08lx arm_lr=%08lx arm_sp=%08lx arm_r0=%08lx arm_r1=%08lx\n",
            static_cast<unsigned long>(m.arm_pc),
            static_cast<unsigned long>(m.arm_lr),
            static_cast<unsigned long>(m.arm_sp),
            static_cast<unsigned long>(m.arm_r0),
            static_cast<unsigned long>(m.arm_r1));
  }
#endif

  (void)write(STDERR_FILENO, "---- /proc/self/maps ----\n", 26);
  DumpFileToStderr("/proc/self/maps");
  (void)write(STDERR_FILENO, "==== end xcsoar crash ====\n", 27);

  signal(signo, SIG_DFL);
  raise(signo);
}

static void
InstallCrashDiagnostics() noexcept
{
  struct sigaction sa;
  sa.sa_sigaction = CrashSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGBUS, &sa, nullptr);
  sigaction(SIGILL, &sa, nullptr);
  sigaction(SIGABRT, &sa, nullptr);
}
#else
static void
InstallCrashDiagnostics() noexcept {}
#endif

static int
Main()
{
  ScreenGlobalInit screen_init;

#if defined(__APPLE__) && !TARGET_OS_IPHONE
  // We do not want the ugly non-localized main menu which SDL creates
  [NSApp setMainMenu: [[NSMenu alloc] init]];
#endif

#ifdef _WIN32
  /* try to make the UI most responsive */
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

  AllowLanguage();
  InitLanguage();

  ScopeGlobalAsioThread global_asio_thread;
  const Net::ScopeInit net_init(asio_thread->GetEventLoop());

  ScopeGlobalPCMMixer global_pcm_mixer(asio_thread->GetEventLoop());
  ScopeGlobalPCMResourcePlayer global_pcm_resouce_player;
  ScopeGlobalVolumeController global_volume_controller;

  // Perform application initialization and run loop
  int ret = EXIT_FAILURE;
  if (Startup(screen_init.GetDisplay()))
    ret = CommonInterface::main_window->RunEventLoop();

  Shutdown();

  DisallowLanguage();

  Fonts::Deinitialize();

  DeinitialiseDataPath();

  return ret;
}

/**
 * Main entry point for the whole XCSoar application
 */
#ifndef _WIN32
int main(int argc, char **argv)
#else
int WINAPI
WinMain(HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance,
        [[maybe_unused]] LPSTR lpCmdLine2,
        [[maybe_unused]] int nCmdShow)
#endif
try {
  InstallCrashDiagnostics();

#ifdef USE_WIN32_RESOURCES
  ResourceLoader::Init(hInstance);
#endif

  // Read options from the command line
  {
#ifdef _WIN32
    Args args(GetCommandLine(), CommandLine::OptionSummary());
#else
    Args args(argc, argv, CommandLine::OptionSummary());
#endif
    CommandLine::Parse(args);
  }

  InitialiseDataPath();

  // Write startup note + version to logfile
  LogFormat("Starting %s", XCSoar_ProductToken);

  int ret = Main();

#if defined(__APPLE__) && TARGET_OS_IPHONE
  /* For some reason, the app process does not exit on iOS, but a black
   * screen remains, if the process is not explicitly terminated */
  exit(ret);
#endif

  return ret;
} catch (...) {
  PrintException(std::current_exception());
  return EXIT_FAILURE;
}
