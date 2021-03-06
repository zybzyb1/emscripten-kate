//
// debugview.cpp
//
// Description: Manages the interaction with GDB
//
//
// Copyright (c) 2008-2010 Ian Wakeling <ian.wakeling@ntlworld.com>
// Copyright (c) 2011 Kåre Särs <kare.sars@iki.fi>
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Library General Public
//  License version 2 as published by the Free Software Foundation.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Library General Public License for more details.
//
//  You should have received a copy of the GNU Library General Public License
//  along with this library; see the file COPYING.LIB.  If not, write to
//  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
//  Boston, MA 02110-1301, USA.

#include "debugview.h"
#include "debugview.moc"

#include <QtCore/QRegExp>
#include <QtCore/QFile>
#include <QtCore/QTimer>

#include <kmessagebox.h>
#include <kurlrequester.h>
#include <kurl.h>
#include <kdebug.h>
#include <klocale.h>

#include <signal.h>
#include <stdlib.h>

static const QString PromptStr = "(prompt)";

DebugView::DebugView( QObject* parent )
:   QObject( parent ),
    m_debugProcess(0),
    m_state( none ),
    m_subState( normal ),
    m_debugLocationChanged( true )
{
}


DebugView::~DebugView()
{
    if (  m_debugProcess.state() != QProcess::NotRunning )
    {
        m_debugProcess.kill();
        m_debugProcess.blockSignals( true );
        m_debugProcess.waitForFinished();
    }
}

void DebugView::runDebugger(const GDBTargetConf &conf, const QStringList &ioFifos)
{
    m_targetConf = conf;
    if (ioFifos.size() == 3) {
        m_ioPipeString = QString("< %1 1> %2 2> %3")
        .arg(ioFifos[0])
        .arg(ioFifos[1])
        .arg(ioFifos[2]);
    }

    if (m_state == none) {
        m_outBuffer.clear();
        m_errBuffer.clear();
        m_errorList.clear();

        //create a process to control GDB
        m_debugProcess.setWorkingDirectory(m_targetConf.workDir);

        connect( &m_debugProcess, SIGNAL(error(QProcess::ProcessError)),
                            this, SLOT(slotError()) );

        connect( &m_debugProcess, SIGNAL(readyReadStandardError()),
                            this, SLOT(slotReadDebugStdErr()) );

        connect( &m_debugProcess, SIGNAL(readyReadStandardOutput()),
                            this, SLOT(slotReadDebugStdOut()) );

        connect( &m_debugProcess, SIGNAL(finished(int,QProcess::ExitStatus)),
                            this, SLOT(slotDebugFinished(int,QProcess::ExitStatus)) );

        m_debugProcess.setShellCommand(m_targetConf.gdbCmd);
        m_debugProcess.setOutputChannelMode(KProcess::SeparateChannels);
        m_debugProcess.start();

        m_nextCommands << "set pagination off";
        m_state = ready;
    }
    else
    {
        // On startup the gdb prompt will trigger the "nextCommands",
        // here we have to trigger it manually.
        QTimer::singleShot(0, this, SLOT(issueNextCommand()));
    }
    m_nextCommands << QString("file %1").arg(m_targetConf.executable);
    m_nextCommands << QString("set args %1 %2").arg(m_targetConf.arguments).arg(m_ioPipeString);
    m_nextCommands << QString("set inferior-tty /dev/null");
    m_nextCommands << m_targetConf.customInit;
    m_nextCommands << QString("(Q) info breakpoints");
}

bool DebugView::debuggerRunning() const
{
    return( m_state != none );
}

bool DebugView::debuggerBusy() const
{
    return( m_state == executingCmd );
}

bool DebugView::hasBreakpoint( const KUrl& url, int line )
{
    for (int i = 0; i<m_breakPointList.size(); i++) {
        if ( (url == m_breakPointList[i].file) && (line == m_breakPointList[i].line) ) {
            return true;
        }
    }
    return false;
}

void DebugView::toggleBreakpoint( KUrl const& url, int line )
{
    if( m_state == ready )
    {
        QString cmd;
        if (hasBreakpoint( url, line ))
        {
            cmd = QString("clear %1:%2").arg(url.path()).arg(line);
        }
        else {
            cmd = QString("break %1:%2").arg(url.path()).arg(line);
        }
        issueCommand( cmd );
    }
}

void DebugView::slotError()
{
    KMessageBox::sorry( NULL, i18n("Could not start debugger process") );
}

void DebugView::slotReadDebugStdOut()
{
    m_outBuffer += QString::fromLocal8Bit( m_debugProcess.readAllStandardOutput().data() );
    int end=0;
    // handle one line at a time
    do {
        end = m_outBuffer.indexOf('\n');
        if (end < 0) break;
        processLine( m_outBuffer.mid(0, end) );
        m_outBuffer.remove(0,end+1);
    } while (1);

    if (m_outBuffer == "(gdb) " || m_outBuffer == ">") 
    {
        m_outBuffer.clear();
        processLine( PromptStr );
    }
}

void DebugView::slotReadDebugStdErr()
{
    m_errBuffer += QString::fromLocal8Bit( m_debugProcess.readAllStandardError().data() );
    int end=0;
    // add whole lines at a time to the error list
    do {
        end = m_errBuffer.indexOf('\n');
        if (end < 0) break;
        m_errorList << m_errBuffer.mid(0, end);
        m_errBuffer.remove(0,end+1);
    } while (1);

    processErrors();
}

void DebugView::slotDebugFinished( int /*exitCode*/, QProcess::ExitStatus status )
{
    if( status != QProcess::NormalExit )
    {
        emit outputText( i18n("*** gdb exited normally ***") + '\n' );
    }

    m_state = none;
    emit readyForInput( false );

    // remove all old breakpoints
    BreakPoint bPoint;
    while ( m_breakPointList.size() > 0 )
    {
        bPoint = m_breakPointList.takeFirst();
        emit breakPointCleared( bPoint.file, bPoint.line -1 );
    }

    emit gdbEnded();
}

void DebugView::movePC( KUrl const& url, int line )
{
    if( m_state == ready )
    {
        QString cmd = QString("tbreak %1:%2").arg(url.path()).arg(line);
        m_nextCommands <<  QString("jump %1:%2").arg(url.path()).arg(line);
        issueCommand( cmd );
    }
}

void DebugView::runToCursor( KUrl const& url, int line )
{
    if( m_state == ready )
    {
        QString cmd = QString("tbreak %1:%2").arg(url.path()).arg(line);
        m_nextCommands << "continue";
        issueCommand( cmd );
    }
}

void DebugView::slotInterrupt()
{
    if (m_state == executingCmd) {
        m_debugLocationChanged = true;
    }
    int pid = m_debugProcess.pid();
    if (pid != 0) {
        ::kill(pid, SIGINT);
    }
}

void DebugView::slotKill()
{
    if( m_state != ready )
    {
        slotInterrupt();
        m_state = ready;
    }
    issueCommand( "kill" );
}

void DebugView::slotReRun()
{
    slotKill();
    m_nextCommands << QString("file %1").arg(m_targetConf.executable);
    m_nextCommands << QString("set args %1 %2").arg(m_targetConf.arguments).arg(m_ioPipeString);
    m_nextCommands << QString("set inferior-tty /dev/null");
    m_nextCommands << m_targetConf.customInit;
    m_nextCommands << QString("(Q) info breakpoints");

    m_nextCommands << QString("tbreak main");
    m_nextCommands << QString("run");
    m_nextCommands << QString("p setvbuf(stdout, 0, %1, 1024)").arg(_IOLBF);
    m_nextCommands << QString("continue");
}

void DebugView::slotStepInto()
{
    issueCommand( "step" );
}

void DebugView::slotStepOver()
{
    issueCommand( "next" );
}

void DebugView::slotStepOut()
{
    issueCommand( "finish" );
}

void DebugView::slotContinue()
{
    issueCommand( "continue" );
}

static QRegExp breakpointList( "Num\\s+Type\\s+Disp\\s+Enb\\s+Address\\s+What.*" );
static QRegExp breakpointListed( "(\\d)\\s+breakpoint\\s+keep\\sy\\s+0x[\\da-f]+\\sin\\s.+\\sat\\s([^:]+):(\\d+).*" );
static QRegExp stackFrameAny( "#(\\d+)\\s(.*)" );
static QRegExp stackFrameFile( "#(\\d+)\\s+(?:0x[\\da-f]+\\s*in\\s)*(\\S+)(\\s\\([^)]*\\))\\sat\\s([^:]+):(\\d+).*" );
static QRegExp changeFile( "(?:(?:Temporary\\sbreakpoint|Breakpoint)\\s*\\d+,\\s*|0x[\\da-f]+\\s*in\\s*)?[^\\s]+\\s*\\([^)]*\\)\\s*at\\s*([^:]+):(\\d+).*" );
static QRegExp changeLine( "(\\d+)\\s+.*" );
static QRegExp breakPointReg( "Breakpoint\\s+(\\d+)\\s+at\\s+0x[\\da-f]+:\\s+file\\s+([^\\,]+)\\,\\s+line\\s+(\\d+).*" );
static QRegExp breakPointMultiReg( "Breakpoint\\s+(\\d+)\\s+at\\s+0x[\\da-f]+:\\s+([^\\,]+):(\\d+).*" );
static QRegExp breakPointDel( "Deleted\\s+breakpoint.*" );
static QRegExp exitProgram( "(?:Program|.*Inferior.*)\\s+exited.*" );
static QRegExp threadLine( "\\**\\s+(\\d+)\\s+Thread.*" );

void DebugView::processLine( QString line )
{
    if (line.isEmpty()) return;

    switch( m_state )
    {
        case none:
        case ready:
            if( PromptStr == line )
            {
                // we get here after initialization
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            break;

        case executingCmd:
            if( breakpointList.exactMatch( line ) )
            {
                m_state = listingBreakpoints;
                emit clearBreakpointMarks();
                m_breakPointList.clear();
            }
            else if ( line.contains( "No breakpoints or watchpoints.") )
            {
                emit clearBreakpointMarks();
                m_breakPointList.clear();
            }
            else if ( stackFrameAny.exactMatch( line ) )
            {
                if ( m_lastCommand.contains( "info stack" ) )
                {
                    emit stackFrameInfo( stackFrameAny.cap(1), stackFrameAny.cap(2));
                }
                else
                {
                    m_subState = ( m_subState == normal ) ? stackFrameSeen : stackTraceSeen;

                    m_newFrameLevel = stackFrameAny.cap( 1 ).toInt();

                    if ( stackFrameFile.exactMatch( line ) )
                    {
                        m_newFrameFile = stackFrameFile.cap( 4 );
                    }
                }
            }
            else if( changeFile.exactMatch( line ) )
            {
                m_currentFile = changeFile.cap( 1 ).trimmed();
                int lineNum = changeFile.cap( 2 ).toInt();

                if ( !m_nextCommands.contains("continue") ) {
                    // GDB uses 1 based line numbers, kate uses 0 based...
                    emit debugLocationChanged( resolveFileName(m_currentFile), lineNum - 1 );
                }
                m_debugLocationChanged = true;
            }
            else if( changeLine.exactMatch( line ) )
            {
                int lineNum = changeLine.cap( 1 ).toInt();

                if( m_subState == stackFrameSeen )
                {
                    m_currentFile = m_newFrameFile;
                }
                if ( !m_nextCommands.contains("continue") ) {
                    // GDB uses 1 based line numbers, kate uses 0 based...
                    emit debugLocationChanged( resolveFileName(m_currentFile), lineNum - 1 );
                }
                m_debugLocationChanged = true;
            }
            else if (breakPointReg.exactMatch(line))
            {
                BreakPoint breakPoint;
                breakPoint.number = breakPointReg.cap( 1 ).toInt();
                breakPoint.file = resolveFileName( breakPointReg.cap( 2 ) );
                breakPoint.line = breakPointReg.cap( 3 ).toInt();
                m_breakPointList << breakPoint;
                emit breakPointSet( breakPoint.file, breakPoint.line -1 );
            }
            else if (breakPointMultiReg.exactMatch(line))
            {
                BreakPoint breakPoint;
                breakPoint.number = breakPointMultiReg.cap( 1 ).toInt();
                breakPoint.file = resolveFileName( breakPointMultiReg.cap( 2 ) );
                breakPoint.line = breakPointMultiReg.cap( 3 ).toInt();
                m_breakPointList << breakPoint;
                emit breakPointSet( breakPoint.file, breakPoint.line -1 );
            }
            else if (breakPointDel.exactMatch(line))
            {
                line.remove("Deleted breakpoint");
                line.remove("s"); // in case of multiple breakpoints
                QStringList numbers = line.split(' ', QString::SkipEmptyParts);
                for (int i=0; i<numbers.size(); i++) 
                {
                    for (int j = 0; j<m_breakPointList.size(); j++) 
                    {
                        if ( numbers[i].toInt() == m_breakPointList[j].number ) 
                        {
                            emit breakPointCleared( m_breakPointList[j].file, m_breakPointList[j].line -1 );
                            m_breakPointList.removeAt(j);
                            break;
                        }
                    }
                }
            }
            else if ( exitProgram.exactMatch( line ) || 
                line.contains( "The program no longer exists" ) ||
                line.contains( "Kill the program being debugged" ) )
            {
                // if there are still commands to execute remove them to remove unneeded output
                // except  if the "kill was for "re-run"
                if ( ( m_nextCommands.size() > 0 ) && !m_nextCommands[0].contains("file") ) 
                {
                    m_nextCommands.clear();
                }
                m_debugLocationChanged = false; // do not insert (Q) commands
                emit programEnded();
            }
            else if( PromptStr == line )
            {
                if( m_subState == stackFrameSeen )
                {
                    emit stackFrameChanged( m_newFrameLevel );
                }
                m_state = ready;

                // Give the error a possibility get noticed since stderr and stdout are not in sync
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            break;

        case listingBreakpoints:
            if (breakpointListed.exactMatch( line ) )
            {
                BreakPoint breakPoint;
                breakPoint.number = breakpointListed.cap( 1 ).toInt();
                breakPoint.file = resolveFileName( breakpointListed.cap( 2 ) );
                breakPoint.line = breakpointListed.cap( 3 ).toInt();
                m_breakPointList << breakPoint;
                emit breakPointSet( breakPoint.file, breakPoint.line -1 );
            }
            else if( PromptStr == line )
            {
                m_state = ready;
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            break;
        case infoArgs:
            if( PromptStr == line )
            {
                m_state = ready;
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            else {
                emit infoLocal( line );
            }
            break;
        case infoLocals:
            if( PromptStr == line )
            {
                m_state = ready;
                emit infoLocal( QString() );
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            else {
                emit infoLocal( line );
            }
            break;
        case infoStack:
            if( PromptStr == line )
            {
                m_state = ready;
                emit stackFrameInfo( QString(), QString() );
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            else if ( stackFrameAny.exactMatch( line ) )
            {
                emit stackFrameInfo( stackFrameAny.cap(1), stackFrameAny.cap(2));
            }
            break;
        case infoThreads:
            if( PromptStr == line )
            {
                m_state = ready;
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            else if ( threadLine.exactMatch( line ) )
            {
                emit threadInfo( threadLine.cap(1).toInt(), (line[0] == '*'));
            }
            break;
    }
    outputTextMaybe( line );
}

void DebugView::processErrors()
{
    QString error;
    while (m_errorList.size() > 0) {
        error = m_errorList.takeFirst();
        //kDebug() << error;
        if( error == "The program is not being run." )
        {
            if ( m_lastCommand == "continue" ) 
            {
                m_nextCommands.clear();
                m_nextCommands << QString("tbreak main");
                m_nextCommands << QString("run");
                m_nextCommands << QString("p setvbuf(stdout, 0, %1, 1024)").arg(_IOLBF);
                m_nextCommands << QString("continue");
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            else if ( ( m_lastCommand == "step" ) ||
                ( m_lastCommand == "next" ) ||
                ( m_lastCommand == "finish" ) )
            {
                m_nextCommands.clear();
                m_nextCommands << "tbreak main";
                m_nextCommands << "run";
                m_nextCommands << QString("p setvbuf(stdout, 0, %1, 1024)").arg(_IOLBF);
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            else if ((m_lastCommand == "kill"))
            {
                if ( m_nextCommands.size() > 0 )
                {
                    if ( !m_nextCommands[0].contains("file") )
                    {
                        m_nextCommands.clear();
                        m_nextCommands << "quit";
                    }
                    // else continue with "ReRun"
                }
                else 
                {
                    m_nextCommands << "quit";
                }
                m_state = ready;
                QTimer::singleShot(0, this, SLOT(issueNextCommand()));
            }
            // else do nothing
        }
        else if ( error.contains( "No line " ) ||
            error.contains( "No source file named" ) )
        {
            // setting a breakpoint failed. Do not continue.
            m_nextCommands.clear();
            emit readyForInput( true );
        }
        else if ( error.contains( "No stack" ) )
        {
            m_nextCommands.clear();
            emit programEnded();
        }
        emit outputError( error + '\n' );
    }
 }

void DebugView::issueCommand( QString const& cmd )
{
    if( m_state == ready )
    {
        emit readyForInput( false );
        m_state = executingCmd;
        if (cmd == "(Q)info locals") {
            m_state = infoLocals;
        }
        else if (cmd == "(Q)info args") {
            m_state = infoArgs;
        }
        else if (cmd == "(Q)info stack") {
            m_state = infoStack;
        }
        else if (cmd == "(Q)info thread") {
            emit threadInfo( -1 , false );
            m_state = infoThreads;
        }
        m_subState = normal;
        m_lastCommand = cmd;

        if ( cmd.startsWith("(Q)") ) 
        {
            m_debugProcess.write( cmd.mid(3).toLocal8Bit() + '\n' );
        }
        else {
            emit outputText( "(gdb) " + cmd + '\n' );
            m_debugProcess.write( cmd.toLocal8Bit() + '\n' );
        }
    }
}

void DebugView::issueNextCommand()
{
    if( m_state == ready )
    {
        if( m_nextCommands.size() > 0)
        {
            QString cmd = m_nextCommands.takeFirst();
            //kDebug() << "Next command" << cmd;
            issueCommand( cmd );
        }
        else 
        {
            // FIXME "thread" needs a better generic solution 
            if (m_debugLocationChanged || m_lastCommand.startsWith("thread")) {
                m_debugLocationChanged = false;
                if (!m_lastCommand.startsWith("(Q)")) {
                    m_nextCommands << "(Q)info stack";
                    m_nextCommands << "(Q)frame";
                    m_nextCommands << "(Q)info args";
                    m_nextCommands << "(Q)info locals";
                    m_nextCommands << "(Q)info thread";
                    issueNextCommand();
                    return;
                }
            }
            emit readyForInput( true );
        }
    }
}

KUrl DebugView::resolveFileName( const QString &fileName )
{
    KUrl url;

    //did we end up with an absolute path or a relative one?
    if ( QFileInfo(fileName).isAbsolute() ) {
        url.setPath( fileName );
        url.cleanPath();
    }
    else {
        url.setPath(m_targetConf.workDir);
        url.addPath(fileName);
        url.cleanPath();

        if (!QFileInfo(url.path()).exists()) {
            url.setPath(m_targetConf.executable);
            url.upUrl(); // get path
            url.addPath(fileName);
            url.cleanPath();
        }
        // Now, if not found just give up ;)
    }

    return url;
}

void DebugView::outputTextMaybe( const QString &text )
{
    if ( !m_lastCommand.startsWith( "(Q)" )  && !text.contains( PromptStr ) )
    {
        emit outputText( text + '\n' );
    }
}
