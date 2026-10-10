// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/protections_button.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ghost/browser/ui/protections/protections_strings.h"
#include "ghost/browser/ui/protections/protections_ui.h"
#include "ghost/browser/ui/protections/shield_off_icon.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace ghost::protections {

DEFINE_ELEMENT_IDENTIFIER_VALUE(kProtectionsButtonElementId);

namespace {

using privacy_policy::ProtectionLevel;

// The shield with the count in a pill over its bottom right corner, cut out
// of the shield so that it reads on any theme.
class ShieldWithCount : public gfx::CanvasImageSource {
 public:
  ShieldWithCount(int size, SkColor icon_color, std::u16string text, SkColor pill_color,
                  SkColor text_color)
      : gfx::CanvasImageSource(gfx::Size(size, size)),
        icon_color_(icon_color),
        text_(std::move(text)),
        pill_color_(pill_color),
        text_color_(text_color) {}

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    gfx::PaintVectorIcon(canvas, vector_icons::kShieldIcon, size().width(), icon_color_);
    const gfx::FontList font =
        gfx::FontList().DeriveWithHeightUpperBound(size().height() / 2).DeriveWithWeight(
            gfx::Font::Weight::BOLD);
    const int height = font.GetHeight();
    const int width = std::max(height, gfx::GetStringWidth(text_, font) + 4);
    const gfx::Rect pill(size().width() - width, size().height() - height, width, height);

    cc::PaintFlags cutout;
    cutout.setAntiAlias(true);
    cutout.setBlendMode(SkBlendMode::kClear);
    gfx::Rect around = pill;
    around.Outset(1);
    canvas->DrawRoundRect(around, around.height() / 2, cutout);

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(pill_color_);
    canvas->DrawRoundRect(pill, pill.height() / 2, fill);
    canvas->DrawStringRectWithFlags(text_, font, text_color_, pill,
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const SkColor icon_color_;
  const std::u16string text_;
  const SkColor pill_color_;
  const SkColor text_color_;
};

std::u16string BadgeText(int count) {
  if (count <= 0) {
    return std::u16string();
  }
  return count > 99 ? u"99+" : base::NumberToString16(count);
}

}  // namespace

ProtectionsButton::ProtectionsButton(BrowserWindowInterface* browser)
    : ToolbarButton(base::BindRepeating(&ProtectionsButton::ShowPanel, base::Unretained(this))),
      browser_(browser),
      // The task manager names the panel's process with an upstream string
      // until Shade's strings have a grd (progress notes, 3D-3).
      bubble_manager_(WebUIBubbleManager::Create<ProtectionsUI>(
          browser, GURL(kProtectionsURL), IDS_SETTINGS_PRIVACY)) {
  SetProperty(views::kElementIdentifierKey, kProtectionsButtonElementId);
  browser_->GetTabStripModel()->AddObserver(this);
  Follow(browser_->GetTabStripModel()->GetActiveWebContents());
}

ProtectionsButton::~ProtectionsButton() = default;

void ProtectionsButton::UpdateIcon() {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  const int size = GetIconSize();
  if (!state_.applies) {
    UpdateIconsWithStandardColors(vector_icons::kShieldIcon);
    return;
  }
  if (IsOff()) {
    const SkColor inactive = colors->GetColor(kColorToolbarButtonIconInactive);
    UpdateIconsWithColors(kShieldOffIcon, inactive, inactive, inactive, inactive);
    return;
  }
  if (badge_text_.empty()) {
    UpdateIconsWithStandardColors(vector_icons::kShieldIcon);
    return;
  }
  for (ButtonState state : {STATE_NORMAL, STATE_HOVERED, STATE_PRESSED, STATE_DISABLED}) {
    SetImageModel(state, ui::ImageModel::FromImageSkia(gfx::ImageSkia(
                             std::make_unique<ShieldWithCount>(
                                 size, GetForegroundColor(state), badge_text_,
                                 colors->GetColor(ui::kColorSysPrimary),
                                 colors->GetColor(ui::kColorSysOnPrimary)),
                             gfx::Size(size, size))));
  }
}

void ProtectionsButton::OnTabStripModelChanged(TabStripModel* tab_strip_model,
                                               const TabStripModelChange& change,
                                               const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    Follow(tab_strip_model->GetActiveWebContents());
  }
}

void ProtectionsButton::OnBlockedCountChanged() {
  Update();
}

void ProtectionsButton::WebContentsDestroyed() {
  // Before the tab's PageProtections goes with it.
  Follow(nullptr);
}

void ProtectionsButton::Follow(content::WebContents* tab) {
  protections_observation_.Reset();
  Observe(tab);
  if (tab) {
    PageProtections::CreateForWebContents(tab);
    protections_observation_.Observe(PageProtections::FromWebContents(tab));
  }
  Update();
}

void ProtectionsButton::Update() {
  state_ = web_contents() ? GetPanelState(*web_contents()) : PanelState();
  badge_text_ = state_.applies && !IsOff() ? BadgeText(state_.blocked_count) : std::u16string();
  SetEnabled(state_.applies);
  if (state_.applies) {
    SetTooltipText(strings::ButtonAccessibleName(state_.level, state_.blocked_count));
  } else {
    SetTooltipText(strings::NotApplicable());
  }
  GetViewAccessibility().SetName(GetTooltipText());
  UpdateIcon();
}

bool ProtectionsButton::IsOff() const {
  return state_.level == ProtectionLevel::kOff;
}

void ProtectionsButton::ShowPanel() {
  bubble_manager_->ShowBubble(this);
}

BEGIN_METADATA(ProtectionsButton)
END_METADATA

std::unique_ptr<views::View> CreateProtectionsButton(BrowserWindowInterface* browser) {
  return std::make_unique<ProtectionsButton>(browser);
}

}  // namespace ghost::protections
