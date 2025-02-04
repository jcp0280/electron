// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/win/notify_icon.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/windows_version.h"
#include "shell/browser/ui/win/notify_icon_host.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/display/screen.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace electron {

NotifyIcon::NotifyIcon(NotifyIconHost* host, UINT id, HWND window, UINT message)
    : host_(host),
      icon_id_(id),
      window_(window),
      message_id_(message),
      weak_factory_(this) {
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags |= NIF_MESSAGE;
  icon_data.uCallbackMessage = message_id_;
  BOOL result = Shell_NotifyIcon(NIM_ADD, &icon_data);
  // This can happen if the explorer process isn't running when we try to
  // create the icon for some reason (for example, at startup).
  if (!result)
    LOG(WARNING) << "Unable to create status tray icon.";
}

NotifyIcon::~NotifyIcon() {
  // Remove our icon.
  host_->Remove(this);
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  Shell_NotifyIcon(NIM_DELETE, &icon_data);
}

void NotifyIcon::HandleClickEvent(int modifiers,
                                  bool left_mouse_click,
                                  bool double_button_click) {
  gfx::Rect bounds = GetBounds();

  if (left_mouse_click) {
    if (double_button_click)  // double left click
      NotifyDoubleClicked(bounds, modifiers);
    else  // single left click
      NotifyClicked(bounds,
                    display::Screen::GetScreen()->GetCursorScreenPoint(),
                    modifiers);
    return;
  } else if (!double_button_click) {  // single right click
    if (menu_model_)
      PopUpContextMenu(gfx::Point(), menu_model_);
    else
      NotifyRightClicked(bounds, modifiers);
  }
}

void NotifyIcon::HandleMouseMoveEvent(int modifiers) {
  gfx::Point cursorPos = display::Screen::GetScreen()->GetCursorScreenPoint();
  // Omit event fired when tray icon is created but cursor is outside of it.
  if (GetBounds().Contains(cursorPos))
    NotifyMouseMoved(cursorPos, modifiers);
}

void NotifyIcon::ResetIcon() {
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  // Delete any previously existing icon.
  Shell_NotifyIcon(NIM_DELETE, &icon_data);
  InitIconData(&icon_data);
  icon_data.uFlags |= NIF_MESSAGE;
  icon_data.uCallbackMessage = message_id_;
  icon_data.hIcon = icon_.get();
  // If we have an image, then set the NIF_ICON flag, which tells
  // Shell_NotifyIcon() to set the image for the status icon it creates.
  if (icon_data.hIcon)
    icon_data.uFlags |= NIF_ICON;
  // Re-add our icon.
  BOOL result = Shell_NotifyIcon(NIM_ADD, &icon_data);
  if (!result)
    LOG(WARNING) << "Unable to re-create status tray icon.";
}

void NotifyIcon::SetImage(HICON image) {
  icon_ = base::win::ScopedHICON(CopyIcon(image));

  // Create the icon.
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags |= NIF_ICON;
  icon_data.hIcon = image;
  BOOL result = Shell_NotifyIcon(NIM_MODIFY, &icon_data);
  if (!result)
    LOG(WARNING) << "Error setting status tray icon image";
}

void NotifyIcon::SetPressedImage(HICON image) {
  // Ignore pressed images, since the standard on Windows is to not highlight
  // pressed status icons.
}

void NotifyIcon::SetToolTip(const std::string& tool_tip) {
  // Create the icon.
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags |= NIF_TIP;
  wcsncpy_s(icon_data.szTip, base::UTF8ToUTF16(tool_tip).c_str(), _TRUNCATE);
  BOOL result = Shell_NotifyIcon(NIM_MODIFY, &icon_data);
  if (!result)
    LOG(WARNING) << "Unable to set tooltip for status tray icon";
}

void NotifyIcon::DisplayBalloon(HICON icon,
                                const base::string16& title,
                                const base::string16& contents) {
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags |= NIF_INFO;
  icon_data.dwInfoFlags = NIIF_INFO;
  wcsncpy_s(icon_data.szInfoTitle, title.c_str(), _TRUNCATE);
  wcsncpy_s(icon_data.szInfo, contents.c_str(), _TRUNCATE);
  icon_data.uTimeout = 0;
  icon_data.hBalloonIcon = icon;
  icon_data.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;

  BOOL result = Shell_NotifyIcon(NIM_MODIFY, &icon_data);
  if (!result)
    LOG(WARNING) << "Unable to create status tray balloon.";
}

void NotifyIcon::RemoveBalloon() {
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags |= NIF_INFO;

  BOOL result = Shell_NotifyIcon(NIM_MODIFY, &icon_data);
  if (!result)
    LOG(WARNING) << "Unable to remove status tray balloon.";
}

void NotifyIcon::PopUpContextMenu(const gfx::Point& pos,
                                  AtomMenuModel* menu_model) {
  // Returns if context menu isn't set.
  if (menu_model == nullptr && menu_model_ == nullptr)
    return;

  // Set our window as the foreground window, so the context menu closes when
  // we click away from it.
  if (!SetForegroundWindow(window_))
    return;

  // Cancel current menu if there is one.
  if (menu_runner_ && menu_runner_->IsRunning())
    menu_runner_->Cancel();

  // Show menu at mouse's position by default.
  gfx::Rect rect(pos, gfx::Size());
  if (pos.IsOrigin())
    rect.set_origin(display::Screen::GetScreen()->GetCursorScreenPoint());

  // Create a widget for the menu, otherwise we get no keyboard events, which
  // is required for accessibility.
  widget_.reset(new views::Widget());
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
  params.ownership =
      views::Widget::InitParams::Ownership::WIDGET_OWNS_NATIVE_WIDGET;
  params.bounds = gfx::Rect(0, 0, 0, 0);
  params.force_software_compositing = true;
  params.z_order = ui::ZOrderLevel::kFloatingUIElement;

  widget_->Init(params);

  widget_->Show();
  widget_->Activate();
  menu_runner_.reset(new views::MenuRunner(
      menu_model != nullptr ? menu_model : menu_model_,
      views::MenuRunner::CONTEXT_MENU | views::MenuRunner::HAS_MNEMONICS,
      base::BindRepeating(&NotifyIcon::OnContextMenuClosed,
                          weak_factory_.GetWeakPtr())));
  menu_runner_->RunMenuAt(widget_.get(), NULL, rect,
                          views::MenuAnchorPosition::kTopLeft,
                          ui::MENU_SOURCE_MOUSE);
}

void NotifyIcon::SetContextMenu(AtomMenuModel* menu_model) {
  menu_model_ = menu_model;
}

gfx::Rect NotifyIcon::GetBounds() {
  NOTIFYICONIDENTIFIER icon_id;
  memset(&icon_id, 0, sizeof(NOTIFYICONIDENTIFIER));
  icon_id.uID = icon_id_;
  icon_id.hWnd = window_;
  icon_id.cbSize = sizeof(NOTIFYICONIDENTIFIER);

  RECT rect = {0};
  Shell_NotifyIconGetRect(&icon_id, &rect);
  return display::win::ScreenWin::ScreenToDIPRect(window_, gfx::Rect(rect));
}

void NotifyIcon::InitIconData(NOTIFYICONDATA* icon_data) {
  memset(icon_data, 0, sizeof(NOTIFYICONDATA));
  icon_data->cbSize = sizeof(NOTIFYICONDATA);
  icon_data->hWnd = window_;
  icon_data->uID = icon_id_;
}

void NotifyIcon::OnContextMenuClosed() {
  widget_->Close();
}

}  // namespace electron
