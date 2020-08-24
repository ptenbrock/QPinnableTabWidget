/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Copyright (C) 2020 Francesc M.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "FileDiffView.h"

#include <GitQlientStyles.h>
#include <FileDiffHighlighter.h>

#include <QPainter>
#include <QTextBlock>
#include <QScrollBar>
#include <QIcon>

#include <QLogger.h>

using namespace QLogger;

FileDiffView::FileDiffView(bool allowComments, QWidget *parent)
   : QPlainTextEdit(parent)
   , mLineNumberArea(new LineNumberArea(this))
   , mDiffHighlighter(new FileDiffHighlighter(document()))
   , mCommentsAllowed(allowComments)
{
   setAttribute(Qt::WA_DeleteOnClose);
   setReadOnly(true);

   if (mCommentsAllowed)
   {
      installEventFilter(this);
      setMouseTracking(true);
   }

   connect(this, &FileDiffView::blockCountChanged, this, &FileDiffView::updateLineNumberAreaWidth);
   connect(this, &FileDiffView::updateRequest, this, &FileDiffView::updateLineNumberArea);
   connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &FileDiffView::signalScrollChanged);
}

FileDiffView::~FileDiffView()
{
   delete mDiffHighlighter;
}

void FileDiffView::loadDiff(QString text, const QVector<DiffInfo::ChunkInfo> &fileDiffInfo)
{
   QLog_Trace("UI",
              QString("FileDiffView::loadDiff - {%1} move scroll to pos {%2}")
                  .arg(objectName(), QString::number(verticalScrollBar()->value())));

   mDiffHighlighter->setDiffInfo(fileDiffInfo);

   const auto pos = verticalScrollBar()->value();
   auto cursor = textCursor();
   const auto tmpCursor = textCursor().position();
   setPlainText(text);

   cursor.setPosition(tmpCursor);
   setTextCursor(cursor);

   blockSignals(true);
   verticalScrollBar()->setValue(pos);
   blockSignals(false);

   emit updateRequest(viewport()->rect(), 0);

   QLog_Trace("UI",
              QString("FileDiffView::loadDiff - {%1} move scroll to pos {%2}").arg(objectName(), QString::number(pos)));
}

void FileDiffView::moveScrollBarToPos(int value)
{
   blockSignals(true);
   verticalScrollBar()->setValue(value);
   blockSignals(false);

   emit updateRequest(viewport()->rect(), 0);

   QLog_Trace("UI",
              QString("FileDiffView::moveScrollBarToPos - {%1} move scroll to pos {%2}")
                  .arg(objectName(), QString::number(value)));
}

int FileDiffView::getHeight() const
{
   auto block = firstVisibleBlock();
   auto height = 0;

   while (block.isValid())
   {
      height += blockBoundingRect(block).height();
      block = block.next();
   }

   return height;
}

int FileDiffView::lineNumberAreaWidth()
{
   auto digits = 6;
   auto max = std::max(1, blockCount() + mStartingLine);

   while (max >= 10)
   {
      max /= 10;
      ++digits;
   }

   int width;

#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
   width = fontMetrics().horizontalAdvance(QLatin1Char('9'));
#else
   width = fontMetrics().boundingRect(QLatin1Char('9')).width();
#endif

   return width * digits;
}

void FileDiffView::updateLineNumberAreaWidth(int /* newBlockCount */)
{
   setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void FileDiffView::updateLineNumberArea(const QRect &rect, int dy)
{
   if (dy != 0)
      mLineNumberArea->scroll(0, dy);
   else
      mLineNumberArea->update(0, rect.y(), mLineNumberArea->width(), rect.height());

   if (rect.contains(viewport()->rect()))
      updateLineNumberAreaWidth(0);
}

void FileDiffView::resizeEvent(QResizeEvent *e)
{
   QPlainTextEdit::resizeEvent(e);

   const auto cr = contentsRect();

   mLineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

bool FileDiffView::eventFilter(QObject *obj, QEvent *event)
{
   if (event->type() == QEvent::Enter || event->type() == QEvent::Move)
   {
      const auto height = mLineNumberArea->width();
      const auto helpPos = mapFromGlobal(QCursor::pos());
      const auto x = helpPos.x();
      if (x >= 0 && x <= height)
      {
         QTextCursor cursor = cursorForPosition(helpPos);
         mRow = cursor.block().blockNumber() + mStartingLine + 1;

         repaint();
      }
   }
   else if (event->type() == QEvent::Leave)
   {
      mRow = -1;
      repaint();
   }

   return QPlainTextEdit::eventFilter(obj, event);
}

void FileDiffView::mouseMoveEvent(QMouseEvent *e)
{
   if (mCommentsAllowed)
   {
      if (mLineNumberArea->rect().contains(e->pos()))
      {
         const auto height = mLineNumberArea->width();
         const auto helpPos = mapFromGlobal(QCursor::pos());
         const auto x = helpPos.x();
         if (x >= 0 && x <= height)
         {
            QTextCursor cursor = cursorForPosition(helpPos);
            mRow = cursor.block().blockNumber() + mStartingLine + 1;

            repaint();
         }
      }
      else
      {
         mRow = -1;
         repaint();
      }
   }
}

void FileDiffView::lineNumberAreaPaintEvent(QPaintEvent *event)
{
   QPainter painter(mLineNumberArea);
   painter.fillRect(event->rect(), QColor(GitQlientStyles::getBackgroundColor()));

   auto block = firstVisibleBlock();
   auto blockNumber = block.blockNumber() + mStartingLine;
   auto top = blockBoundingGeometry(block).translated(contentOffset()).top();
   auto bottom = top + blockBoundingRect(block).height();
   auto lineCorrection = 0;

   auto offset = 0;
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
   offset = fontMetrics().horizontalAdvance(QLatin1Char(' '));
#else
   offset = fontMetrics().boundingRect(QLatin1Char(' ')).width();
#endif

   while (block.isValid() && top <= event->rect().bottom())
   {

      if (block.isVisible() && bottom >= event->rect().top())
      {
         const auto skipDeletion = mUnified && !block.text().startsWith("-") && !block.text().startsWith("@");

         if (!mUnified || skipDeletion)
         {
            const auto number = blockNumber + 1 + lineCorrection;
            painter.setPen(GitQlientStyles::getTextColor());

            if (mRow == number)
            {
               const auto width = mLineNumberArea->width();
               const auto height = fontMetrics().height();
               painter.drawPixmap(width - height, static_cast<int>(top), height, height,
                                  QIcon(":/icons/add_comment").pixmap(height, height));
            }

            painter.drawText(0, static_cast<int>(top), mLineNumberArea->width() - offset * 3, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number));
         }
         else
            --lineCorrection;
      }

      block = block.next();
      top = bottom;
      bottom = top + blockBoundingRect(block).height();
      ++blockNumber;
   }
}

FileDiffView::LineNumberArea::LineNumberArea(FileDiffView *editor)
   : QWidget(editor)
{
   fileDiffWidget = editor;
   setMouseTracking(true);
}

QSize FileDiffView::LineNumberArea::sizeHint() const
{
   return { fileDiffWidget->lineNumberAreaWidth(), 0 };
}

void FileDiffView::LineNumberArea::paintEvent(QPaintEvent *event)
{
   fileDiffWidget->lineNumberAreaPaintEvent(event);
}

void FileDiffView::LineNumberArea::mouseMoveEvent(QMouseEvent *e)
{
   fileDiffWidget->mouseMoveEvent(e);
}

void FileDiffView::LineNumberArea::mousePressEvent(QMouseEvent *e)
{
   if (fileDiffWidget->mCommentsAllowed)
      mPressed = rect().contains(e->pos());
}
#include <QMessageBox>
void FileDiffView::LineNumberArea::mouseReleaseEvent(QMouseEvent *e)
{
   if (fileDiffWidget->mCommentsAllowed && mPressed && rect().contains(e->pos())) { }

   mPressed = false;
}
