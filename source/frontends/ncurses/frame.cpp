#include "frontends/ncurses/frame.h"

Frame::Frame() : myColumns(-1)
{
  init(40);

  const int rows = 28;
  const int cols = 30;

  const int xpos = 0;
  const int ypos = 0;

  myBuffer.reset(newwin(rows, cols, ypos + 1, xpos + 1), delwin);
  scrollok(myBuffer.get(), true);
  wrefresh(myBuffer.get());

  myBorders.reset(newwin(1 + rows + 1, 1 + cols + 1, ypos, xpos), delwin);
  box(myBorders.get(), 0 , 0);
  wrefresh(myBorders.get());
}

void Frame::init(int columns)
{
  if (myColumns != columns)
  {
    if (columns < myColumns)
    {
      werase(myStatus.get());
      wrefresh(myStatus.get());
      werase(myFrame.get());
      wrefresh(myFrame.get());
    }

    myColumns = columns;

    const int width = 1 + myColumns + 1;
    const int left = (COLS - width) / 2;

    myFrame.reset(newwin(1 + 24 + 1, width, 0, left), delwin);
    box(myFrame.get(), 0 , 0);
    wtimeout(myFrame.get(), 0);
    keypad(myFrame.get(), true);
    wrefresh(myFrame.get());

    myStatus.reset(newwin(4, width, 1 + 24 + 1, left), delwin);
    box(myStatus.get(), 0 , 0);
    wrefresh(myStatus.get());
  }

}

WINDOW * Frame::getWindow()
{
  return myFrame.get();
}

WINDOW * Frame::getBuffer()
{
  return myBuffer.get();
}

WINDOW * Frame::getStatus()
{
  return myStatus.get();
}

int Frame::getColumns() const
{
  return myColumns;
}