/***************************************************************************
 *   Copyright (C) 2005-08 by the Quassel Project                          *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <QDateTime>
#include <QString>
#include <QTimer>
#include <QTranslator>
#include <QFile>

#include "global.h"
#include "logger.h"
#include "network.h"
#include "settings.h"
#include "cliparser.h"

#if defined BUILD_CORE
#include <QDir>
#include "core.h"
#include "message.h"

#elif defined BUILD_QTUI
#include "client.h"
#include "qtuiapplication.h"
#include "qtui.h"

#elif defined BUILD_MONO
#include "client.h"
#include "core.h"
#include "coresession.h"
#include "qtuiapplication.h"
#include "qtui.h"

#else
#error "Something is wrong - you need to #define a build mode!"
#endif


#include <signal.h>

#ifndef Q_OS_WIN32
#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#endif

//! Signal handler for graceful shutdown.
void handle_signal(int sig) {
  qWarning("%s", qPrintable(QString("Caught signal %1 - exiting.").arg(sig)));
  QCoreApplication::quit();
}

#ifndef Q_OS_WIN32
void handle_crash(int sig) {
  void* callstack[128];
  int i, frames = backtrace(callstack, 128);

  QFile dumpFile(QString("Quassel-Crash-%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmm.log")));
  dumpFile.open(QIODevice::WriteOnly);
  QTextStream dumpStream(&dumpFile);

  for (i = 0; i < frames; ++i) {
    Dl_info info;
    dladdr (callstack[i], &info);
    // as a reference:
    //     typedef struct
    //     {
    //       __const char *dli_fname;	/* File name of defining object.  */
    //       void *dli_fbase;		/* Load address of that object.  */
    //       __const char *dli_sname;	/* Name of nearest symbol.  */
    //       void *dli_saddr;		/* Exact value of nearest symbol.  */
    //     } Dl_info;

#if __LP64__
    int addrSize = 16;
#else
    int addrSize = 8;
#endif

    QString funcName;
    if(info.dli_sname) {
      char *func = abi::__cxa_demangle(info.dli_sname, 0, 0, 0);
      if(func) {
	funcName = QString(func);
	free(func);
      } else {
	funcName = QString(info.dli_sname);
      }
    } else {
      funcName = QString("0x%1").arg((long)info.dli_saddr, addrSize, QLatin1Char('0'));
    }

    // prettificating the filename
    QString fileName("???");
    if(info.dli_fname) {
      fileName = QString(info.dli_fname);
      int slashPos = fileName.lastIndexOf('/');
      if(slashPos != -1)
	fileName = fileName.mid(slashPos + 1);
      if(fileName.count() < 20)
	fileName += QString(20 - fileName.count(), ' ');
    }

    QString debugLine = QString("#%1 %2 0x%3 %4").arg(i, 3, 10)
      .arg(fileName)
      .arg((long)(callstack[i]), addrSize, 16, QLatin1Char('0'))
      .arg(funcName);

    dumpStream << debugLine << "\n";
    qDebug() << qPrintable(debugLine);
  }
  dumpFile.close();
  exit(27);
}
#endif // ifndef Q_OS_WIN32


int main(int argc, char **argv) {
  // We catch SIGTERM and SIGINT (caused by Ctrl+C) to graceful shutdown Quassel.
  signal(SIGTERM, handle_signal);
  signal(SIGINT, handle_signal);

#ifndef Q_OS_WIN32
  signal(SIGABRT, handle_crash);
  signal(SIGBUS, handle_crash);
  signal(SIGSEGV, handle_crash);
#endif // ndef Q_OS_WIN32
  
  Global::registerMetaTypes();
  Global::setupVersion();

/*
#if defined BUILD_CORE
  Global::runMode = Global::CoreOnly;
  QCoreApplication app(argc, argv);
#elif defined BUILD_QTUI
  Global::runMode = Global::ClientOnly;
  QApplication app(argc, argv);
#else
  Global::runMode = Global::Monolithic;
  QApplication app(argc, argv);
#endif
*/
#if defined BUILD_CORE
  Global::runMode = Global::CoreOnly;
  QCoreApplication app(argc, argv);
#elif defined BUILD_QTUI
  Global::runMode = Global::ClientOnly;
  QtUiApplication app(argc, argv);
#else
  Global::runMode = Global::Monolithic;
  QtUiApplication app(argc, argv);
#endif



  Global::parser = CliParser(QCoreApplication::arguments());

#ifndef BUILD_QTUI
// put core-only arguments here
  Global::parser.addOption("port",'p',"The port quasselcore will listen at",QString("4242"));
  Global::parser.addSwitch("norestore", 'n', "Don't restore last core's state");
  Global::parser.addOption("logfile",'l',"Path to logfile");
  Global::parser.addOption("loglevel",'L',"Loglevel Debug|Info|Warning|Error","Info");
  Global::parser.addOption("datadir", 0, "Specify the directory holding datafiles like the Sqlite DB and the SSL Cert");
#endif // BUILD_QTUI
#ifndef BUILD_CORE
// put client-only arguments here
  Global::parser.addSwitch("debugbufferswitches",0,"Enables debugging for bufferswitches");
  Global::parser.addSwitch("debugmodel",0,"Enables debugging for models");
#endif // BUILD_QTCORE
// put shared client&core arguments here
  Global::parser.addSwitch("debug",'d',"Enable debug output");
  Global::parser.addSwitch("help",'h', "Display this help and exit");

  if(!Global::parser.parse() || Global::parser.isSet("help")) {
    Global::parser.usage();
    return 1;
  }

  /*
   This is an initial check if logfile is writable since the warning would spam stdout if done
   in current Logger implementation. Can be dropped whenever the logfile is only opened once.
  */
  if(Global::runMode != Global::ClientOnly) {
    QFile logFile;
    if(!Global::parser.value("logfile").isEmpty()) {
      logFile.setFileName(Global::parser.value("logfile"));
      if(!logFile.open(QIODevice::Append | QIODevice::Text))
        qWarning("Warning: Couldn't open logfile '%s' - will log to stdout instead",qPrintable(logFile.fileName()));
      else logFile.close();
    }
  }

  qsrand(QTime(0,0,0).secsTo(QTime::currentTime()));

  // Set up i18n support
  QLocale locale = QLocale::system();

  QTranslator qtTranslator(&app);
  qtTranslator.setObjectName("QtTr");
  qtTranslator.load(QString(":i18n/qt_%1").arg(locale.name()));
  app.installTranslator(&qtTranslator);

  QTranslator quasselTranslator(&app);
  quasselTranslator.setObjectName("QuasselTr");
  quasselTranslator.load(QString(":i18n/quassel_%1").arg(locale.name()));
  app.installTranslator(&quasselTranslator);

  Network::setDefaultCodecForServer("ISO-8859-1");
  Network::setDefaultCodecForEncoding("UTF-8");
  Network::setDefaultCodecForDecoding("ISO-8859-15");

  QCoreApplication::setOrganizationDomain("quassel-irc.org");
  QCoreApplication::setApplicationName("Quassel IRC");
  QCoreApplication::setOrganizationName("Quassel Project");

  
#ifndef BUILD_QTUI
  Core::instance();  // create and init the core
#endif

  //Settings::init();

#ifndef BUILD_CORE
  // session resume
  QtUi *gui = new QtUi();
  Client::init(gui);
  // init gui only after the event loop has started
  QTimer::singleShot(0, gui, SLOT(init()));
  //gui->init();
#endif

#ifndef BUILD_QTUI
  if(!Global::parser.isSet("norestore")) {
    Core::restoreState();
  }
#endif

#ifndef BUILD_CORE 
  app.resumeSessionIfPossible();
#endif
  
  int exitCode = app.exec();

#ifndef BUILD_QTUI
  Core::saveState();
#endif

#ifndef BUILD_CORE
  // the mainWin has to be deleted before the Core
  // if not Quassel will crash on exit under certain conditions since the gui
  // still wants to access clientdata
  delete gui;
  Client::destroy();
#endif
#ifndef BUILD_QTUI
  Core::destroy();
#endif

  return exitCode;
}
