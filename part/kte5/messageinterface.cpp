/* This file is part of the KDE project
 *
 *   Copyright (C) 2012 Dominik Haumann <dhaumann@kde.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "messageinterface.h"

namespace KTextEditor {

class MessagePrivate
{
  public:
    QList<QAction*> actions;
    Message::MessageType messageType;
    Message::MessagePosition position;
    QString text;
    bool wordWrap;
    int autoHide;
    int priority;
    KTextEditor::View* view;
    KTextEditor::Document* document;
};

Message::Message(MessageType type, const QString& richtext)
  : d(new MessagePrivate())
{
  d->messageType = type;
  d->position = Message::AboveView;
  d->text = richtext;
  d->wordWrap = false;
  d->autoHide = -1;
  d->priority = 0;
  d->view = 0;
  d->document = 0;
}

Message::~Message()
{
  emit closed(this);

  delete d;
}

QString Message::text() const
{
  return d->text;
}

Message::MessageType Message::messageType() const
{
  return d->messageType;
}

void Message::addAction(QAction* action, bool closeOnTrigger)
{
  // make sure this is the parent, so all actions are deleted in the destructor
  action->setParent(this);
  d->actions.append(action);

  // call close if wanted
  if (closeOnTrigger)
    connect(action, SIGNAL(triggered()), SLOT(deleteLater()));
}

QList<QAction*> Message::actions() const
{
  return d->actions;
}

void Message::setAutoHide(int autoHideTimer)
{
  d->autoHide = autoHideTimer;
}

int Message::autoHide() const
{
  return d->autoHide;
}

void Message::setWordWrap(bool wordWrap)
{
  d->wordWrap = wordWrap;
}

bool Message::wordWrap() const
{
  return d->wordWrap;
}

void Message::setPriority(int priority)
{
  d->priority = priority;
}

int Message::priority() const
{
  return d->priority;
}

void Message::setView(KTextEditor::View* view)
{
  d->view = view;
}

KTextEditor::View* Message::view() const
{
  return d->view;
}

void Message::setDocument(KTextEditor::Document* document)
{
  d->document = document;
}

KTextEditor::Document* Message::document() const
{
  return d->document;
}

void Message::setPosition(Message::MessagePosition position)
{
  d->position = position;
}

Message::MessagePosition Message::position() const
{
  return d->position;
}



MessageInterface::MessageInterface()
  : d (0)
{
}

MessageInterface::~MessageInterface()
{
}

}

// kate: space-indent on; indent-width 2; replace-tabs on;
