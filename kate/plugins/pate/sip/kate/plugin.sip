//
//     Copyright 2007 Jim Bublitz <jbublitz@nwinternet.com>
//     Earlier copyrights 1998 - 2006 Jim Bublitz also apply


//                 Generated by preSip
//            PyKDE4 module kate  version KDE 3.92.0


// This file is part of PyKDE4.

// PyKDE4 is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of
// the License, or (at your option) any later version.

// PyKDE4 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


namespace Kate
{

class Plugin : QObject
{
%TypeHeaderCode
#include <plugin.h>
%End

public:
                         Plugin (Kate::Application* = 0, const char* = 0);
    Kate::Application*   application () const;
    virtual Kate::PluginView* createView (Kate::MainWindow*);
    virtual void         readSessionConfig (KConfigBase*, const QString&);
    virtual void         writeSessionConfig (KConfigBase*, const QString&);

};  // class Plugin

Kate::Plugin*        createPlugin (const char*, Kate::Application* = 0, const QStringList& = QStringList ());

class PluginView : QObject
{
%TypeHeaderCode
#include <plugin.h>
%End

public:
                         PluginView (Kate::MainWindow*);
    Kate::MainWindow*    mainWindow () const;
    virtual void         readSessionConfig (KConfigBase*, const QString&);
    virtual void         writeSessionConfig (KConfigBase*, const QString&);

};  // class PluginView

};  // namespace Kate


