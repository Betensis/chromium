// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"

#include <vector>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/check_is_test.h"
#include "base/compiler_specific.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/case_conversion.h"
#include "base/notreached.h"
#include "base/observer_list.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/image_fetcher/image_fetcher_service_factory.h"
#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_service.h"
#include "chrome/browser/new_tab_page/bitrix24_auth/bitrix24_auth_service_factory.h"
#include "chrome/browser/profiles/avatar_menu.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_key.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/sync/sync_ui_util.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/profiles/profile_colors_util.h"
#include "chrome/browser/ui/signin/dice_migration_service.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button_state_manager.h"
#include "chrome/browser/ui/views/profiles/profile_menu_coordinator.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_ink_drop_util.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/user_education/user_education_service.h"
#include "chrome/browser/user_education/user_education_service_factory.h"
#include "chrome/browser/webauthn/passkey_unlock_manager.h"
#include "chrome/browser/webauthn/passkey_unlock_manager_factory.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/tracker.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "components/image_fetcher/core/image_fetcher_service.h"
#include "components/password_manager/content/common/web_ui_constants.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/signin_prefs.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/signin/public/identity_manager/tribool.h"
#include "components/sync/base/features.h"
#include "components/user_education/common/user_education_class_properties.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_utils.h"
#include "google_apis/gaia/gaia_id.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model_utils.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/theme_provider.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/gfx/text_constants.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button_controller.h"
#include "ui/views/controls/button/label_button_border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/style/typography_provider.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kChromeRefreshImageLabelPadding = 6;

// Value used to enlarge the AvatarIcon to accommodate for DIP scaling.
constexpr int kAvatarIconEnlargement = 1;

constexpr SkColor kBitrixBubbleBackground = SkColorSetRGB(5, 22, 61);
constexpr SkColor kBitrixBubbleSurface = SkColorSetRGB(10, 39, 91);
constexpr SkColor kBitrixAccent = SkColorSetRGB(45, 196, 246);
constexpr SkColor kBitrixAccentDark = SkColorSetRGB(4, 35, 72);
constexpr SkColor kBitrixText = SkColorSetRGB(246, 250, 255);
constexpr SkColor kBitrixTextMuted = SkColorSetRGB(157, 181, 218);
constexpr SkColor kBitrixStroke = SkColorSetRGB(51, 102, 164);

std::u16string GetBitrixProfileInitials(std::string_view profile_name) {
  const std::vector<std::u16string> name_parts = base::SplitString(
      base::UTF8ToUTF16(profile_name), base::kWhitespaceUTF16,
      base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (name_parts.empty()) {
    return u"B";
  }

  std::u16string initials(1, name_parts.front().front());
  if (name_parts.size() > 1) {
    initials.push_back(name_parts.back().front());
  }
  return base::i18n::ToUpper(initials);
}

ui::ImageModel CreateBitrixInitialsIcon(std::u16string_view initials,
                                        int icon_size) {
  gfx::Canvas canvas(gfx::Size(icon_size, icon_size), 1.0f,
                     /*is_opaque=*/false);
  cc::PaintFlags circle_flags;
  circle_flags.setAntiAlias(true);
  circle_flags.setColor(kBitrixAccent);
  canvas.DrawCircle(gfx::PointF(icon_size / 2.0f, icon_size / 2.0f),
                    icon_size / 2.0f, circle_flags);

  const auto& font_list = views::TypographyProvider::Get().GetFont(
      views::style::TextContext::CONTEXT_DIALOG_BODY_TEXT,
      views::style::TextStyle::STYLE_CAPTION_MEDIUM);
  canvas.DrawStringRectWithFlags(
      std::u16string(initials), font_list, kBitrixAccentDark,
      gfx::Rect(icon_size, icon_size), gfx::Canvas::TEXT_ALIGN_CENTER);
  return ui::ImageModel::FromImageSkia(
      gfx::ImageSkia::CreateFrom1xBitmap(canvas.GetBitmap()));
}

constexpr net::NetworkTrafficAnnotationTag kBitrix24AvatarTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("bitrix24_portal_avatar", R"(
      semantics {
        sender: "Bitrix24 portal profile popup"
        description:
          "Downloads the signed-in user's avatar from their selected "
          "Bitrix24 cloud portal for display in the browser profile popup."
        trigger: "The user opens the Bitrix24 profile popup."
        data: "The public avatar URL returned by Bitrix24 Network."
        destination: OTHER
        destination_other: "The user's selected Bitrix24 cloud portal"
      }
      policy {
        cookies_allowed: NO
        setting: "The user can sign out from the Bitrix24 profile popup."
        policy_exception_justification: "Not implemented for this MVP."
      })");

class Bitrix24ProfileBubbleView : public views::BubbleDialogDelegate {
 public:
  Bitrix24ProfileBubbleView(AvatarToolbarButton* anchor,
                            Browser* browser,
                            base::RepeatingClosure start_login,
                            base::RepeatingClosure sign_out,
                            base::RepeatingClosure open_portal)
      : BubbleDialogDelegate(anchor,
                             views::BubbleBorder::TOP_RIGHT,
                             views::BubbleBorder::DIALOG_SHADOW,
                             true),
        service_(Bitrix24AuthServiceFactory::GetForProfile(
            browser->profile()->GetOriginalProfile())),
        start_login_(std::move(start_login)),
        sign_out_(std::move(sign_out)),
        open_portal_(std::move(open_portal)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetShowCloseButton(false);
    SetBackgroundColor(kBitrixBubbleBackground);
    set_fixed_width(360);

    auto contents = std::make_unique<views::BoxLayoutView>();
    contents->SetOrientation(views::BoxLayout::Orientation::kVertical);
    contents->SetBackground(
        views::CreateRoundedRectBackground(kBitrixBubbleBackground, 20));
    contents->SetProperty(views::kMarginsKey, gfx::Insets::VH(20, 22));
    BuildContents(contents.get());
    SetContentsView(std::move(contents));
    FetchAvatar(browser->profile()->GetOriginalProfile());
  }

  Bitrix24ProfileBubbleView(const Bitrix24ProfileBubbleView&) = delete;
  Bitrix24ProfileBubbleView& operator=(const Bitrix24ProfileBubbleView&) =
      delete;
  ~Bitrix24ProfileBubbleView() override = default;

 private:
  void BuildContents(views::BoxLayoutView* contents) {
    const bool signed_in =
        service_->state() == Bitrix24AuthService::State::kSignedIn;
    const bool authorizing =
        service_->state() == Bitrix24AuthService::State::kAuthorizing;

    auto* profile_row =
        contents->AddChildView(std::make_unique<views::BoxLayoutView>());
    profile_row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    profile_row->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    profile_row->SetBetweenChildSpacing(14);

    std::u16string profile_name = base::UTF8ToUTF16(service_->profile_name());
    const std::u16string initials =
        GetBitrixProfileInitials(service_->profile_name());
    auto avatar_container = std::make_unique<views::View>();
    avatar_container->SetPreferredSize(gfx::Size(48, 48));
    avatar_container->SetLayoutManager(std::make_unique<views::FillLayout>());

    auto avatar_fallback = std::make_unique<views::Label>(initials);
    avatar_fallback->SetBackground(
        views::CreateRoundedRectBackground(kBitrixAccent, 24));
    avatar_fallback->SetEnabledColor(kBitrixAccentDark);
    avatar_fallback->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    avatar_fallback->SetTextStyle(views::style::STYLE_BODY_3_EMPHASIS);
    avatar_fallback_ =
        avatar_container->AddChildView(std::move(avatar_fallback));

    auto avatar_image = std::make_unique<views::ImageView>();
    avatar_image->SetImageSize(gfx::Size(48, 48));
    avatar_image->SetCornerRadius(24);
    avatar_image->SetVisible(false);
    avatar_image_ = avatar_container->AddChildView(std::move(avatar_image));
    profile_row->AddChildView(std::move(avatar_container));

    auto* details =
        profile_row->AddChildView(std::make_unique<views::BoxLayoutView>());
    details->SetOrientation(views::BoxLayout::Orientation::kVertical);
    details->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kStart);

    auto* title = details->AddChildView(std::make_unique<views::Label>(
        signed_in ? (profile_name.empty() ? u"Профиль Bitrix24" : profile_name)
                  : u"Bitrix24"));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetTextStyle(views::style::STYLE_BODY_3_EMPHASIS);
    title->SetEnabledColor(kBitrixText);

    std::u16string subtitle;
    if (signed_in) {
      subtitle = base::UTF8ToUTF16(service_->portal_origin());
    } else if (authorizing) {
      subtitle = u"Ожидаем подтверждение авторизации";
    } else if (service_->state() == Bitrix24AuthService::State::kError) {
      subtitle = u"Не удалось подключиться. Попробуйте снова";
    } else {
      subtitle = u"Подключите облачный портал";
    }
    auto* portal =
        details->AddChildView(std::make_unique<views::Label>(subtitle));
    portal->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    portal->SetEnabledColor(kBitrixTextMuted);

    auto* actions =
        contents->AddChildView(std::make_unique<views::BoxLayoutView>());
    actions->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    actions->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kEnd);
    actions->SetBetweenChildSpacing(10);
    actions->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(20, 62, 0, 0));

    if (signed_in) {
      auto* portal_button =
          actions->AddChildView(std::make_unique<views::MdTextButton>(
              open_portal_, u"Открыть портал"));
      StyleActionButton(portal_button, true);
      auto* sign_out_button =
          actions->AddChildView(std::make_unique<views::MdTextButton>(
              sign_out_, u"Выйти"));
      StyleActionButton(sign_out_button, false);
    } else {
      auto* login = actions->AddChildView(std::make_unique<views::MdTextButton>(
          start_login_,
          authorizing ? u"Авторизация открыта" : u"Войти в Bitrix24"));
      StyleActionButton(login, true);
      login->SetEnabled(!authorizing);
    }
  }

  void StyleActionButton(views::MdTextButton* button, bool primary) {
    button->SetCornerRadius(14);
    button->SetCustomPadding(gfx::Insets::VH(10, 16));
    button->SetBgColorOverrideDeprecated(
        primary ? std::optional<SkColor>(kBitrixAccent)
                : std::optional<SkColor>(kBitrixBubbleSurface));
    button->SetStrokeColorOverrideDeprecated(kBitrixStroke);
    button->SetEnabledTextColors(primary ? kBitrixAccentDark : kBitrixText);
  }

  void FetchAvatar(Profile* profile) {
    const GURL avatar_url(service_->profile_avatar_url());
    if (!avatar_url.is_valid()) {
      return;
    }
    image_fetcher::ImageFetcherService* image_fetcher_service =
        ImageFetcherServiceFactory::GetForKey(profile->GetProfileKey());
    image_fetcher::ImageFetcher* image_fetcher =
        image_fetcher_service->GetImageFetcher(
            image_fetcher::ImageFetcherConfig::kNetworkOnly);
    image_fetcher->FetchImage(
        avatar_url,
        base::BindOnce(&Bitrix24ProfileBubbleView::OnAvatarFetched,
                       weak_factory_.GetWeakPtr()),
        image_fetcher::ImageFetcherParams(kBitrix24AvatarTrafficAnnotation,
                                          "Bitrix24PortalAvatar"));
  }

  void OnAvatarFetched(const gfx::Image& image,
                       const image_fetcher::RequestMetadata& metadata) {
    if (image.IsEmpty() || !avatar_image_ || !avatar_fallback_) {
      return;
    }
    avatar_image_->SetImage(ui::ImageModel::FromImage(image));
    avatar_image_->SetVisible(true);
    avatar_fallback_->SetVisible(false);
  }

  const raw_ptr<Bitrix24AuthService> service_;
  raw_ptr<views::ImageView> avatar_image_ = nullptr;
  raw_ptr<views::Label> avatar_fallback_ = nullptr;
  const base::RepeatingClosure start_login_;
  const base::RepeatingClosure sign_out_;
  const base::RepeatingClosure open_portal_;
  base::WeakPtrFactory<Bitrix24ProfileBubbleView> weak_factory_{this};
};

}  // namespace

AvatarToolbarButton::AvatarToolbarButton(BrowserView* browser_view)
    : ToolbarButton(base::BindRepeating(&AvatarToolbarButton::ButtonPressed,
                                        base::Unretained(this),
                                        /*is_source_accelerator=*/false)),
      state_manager_(*this, browser_view->browser()),
      slide_animation_(this) {
  state_manager_.InitializeStates();
#if BUILDFLAG(IS_CHROMEOS)
  // On CrOS this button should only show as badging for Incognito, Guest and
  // captivie portal signin. It's only enabled for non captive portal Incognito
  // where a menu is available for closing all Incognito windows.
  Profile* profile = browser_view->browser()->profile();
  CHECK(profile);
  SetEnabled(profile->IsOffTheRecord() && !profile->IsGuestSession() &&
             !profile->GetOTRProfileID().IsCaptivePortal());
#endif  // BUILDFLAG(IS_CHROMEOS)

  // Activate on press for left-mouse-button only to mimic other MenuButtons
  // without drag-drop actions (specifically the adjacent browser menu).
  button_controller()->set_notify_action(
      views::ButtonController::NotifyAction::kOnPress);
  SetTriggerableEventFlags(ui::EF_LEFT_MOUSE_BUTTON);

  SetID(VIEW_ID_AVATAR_BUTTON);
  SetProperty(views::kElementIdentifierKey, kToolbarAvatarButtonElementId);

  // The avatar should not flip with RTL UI. This does not affect text rendering
  // and LabelButton image/label placement is still flipped like usual.
  SetFlipCanvasOnPaintForRTLUI(false);

  GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);

  // For consistency with identity representation, we need to have the avatar on
  // the left and the (potential) user name on the right.
  SetHorizontalAlignment(gfx::ALIGN_LEFT);

  SetImageLabelSpacing(kChromeRefreshImageLabelPadding);
  label()->SetPaintToLayer();
  label()->SetSkipSubpixelRenderingOpacityCheck(true);
  label()->layer()->SetFillsBoundsOpaquely(false);
  label()->SetSubpixelRenderingEnabled(false);

  // With default (EASE_OUT) tween type.
  slide_animation_.SetSlideDuration(base::Milliseconds(200));
}

AvatarToolbarButton::~AvatarToolbarButton() = default;

void AvatarToolbarButton::UpdateIcon() {
  // If the widget is not set yet, the button doesn't have access to the theme
  // provider to set colors. Defer updating until AddedToWidget(). This may get
  // called as a result of OnUserIdentityChanged() called from the constructor
  // when the button is not yet added to the ToolbarView's hierarchy.
  if (!GetWidget()) {
    return;
  }

  const int icon_size = GetIconSize();
  const ui::ColorProvider* const color_provider = GetColorProvider();
  CHECK(color_provider);
  StateProvider* state_provider = state_manager_.GetActiveStateProvider();
  auto [icon, icon_type] = state_provider->GetAvatarIcon(
      icon_size, GetForegroundColor(ButtonState::STATE_NORMAL),
      *color_provider);

  Browser* browser = state_manager_.browser();
  if (browser && browser->profile()->IsRegularProfile()) {
    Bitrix24AuthService* service = Bitrix24AuthServiceFactory::GetForProfile(
        browser->profile()->GetOriginalProfile());
    if (service->state() == Bitrix24AuthService::State::kSignedIn) {
      icon = CreateBitrixInitialsIcon(
          GetBitrixProfileInitials(service->profile_name()), icon_size);
    }
  }

  SetImageModel(ButtonState::STATE_NORMAL, icon);
  SetImageModel(ButtonState::STATE_DISABLED,
                ui::GetDefaultDisabledIconFromImageModel(icon));

  // In forced-colors mode, re-color the placeholder avatar for
  // hover/pressed/highlighted states so it remains visible against the
  // opaque ink drop background. Cache both icons so
  // OnInkDropHighlightedChanged() can swap them cheaply.
  const ui::NativeTheme* theme = GetNativeTheme();
  if (theme &&
      theme->forced_colors() != ui::ColorProviderKey::ForcedColors::kNone &&
      icon_type == AvatarIconType::kPlaceholder) {
    forced_colors_normal_icon_ = icon;
    const SkColor hovered_color =
        color_provider->GetColor(ui::kColorIconHovered);
    forced_colors_hovered_icon_ =
        ui::ImageModel::FromImage(profiles::GetSizedAvatarIcon(
            profiles::GetPlaceholderAvatarIconWithColors(
                hovered_color, hovered_color, icon_size,
                profiles::PlaceholderAvatarIconParams{.has_padding = false,
                                                      .has_background = false}),
            icon_size, icon_size, profiles::SHAPE_CIRCLE));
    SetImageModel(ButtonState::STATE_HOVERED, forced_colors_hovered_icon_);
    SetImageModel(ButtonState::STATE_PRESSED, forced_colors_hovered_icon_);

    // Also override STATE_NORMAL when the ink drop is highlighted
    // (e.g. profile menu bubble is open).
    OnInkDropHighlightedChanged();
  } else {
    forced_colors_normal_icon_ = ui::ImageModel();
    forced_colors_hovered_icon_ = ui::ImageModel();
    SetImageModel(ButtonState::STATE_HOVERED, std::nullopt);
    SetImageModel(ButtonState::STATE_PRESSED, std::nullopt);
  }

  // Update the layout insets as the new icon might have caused them to change
  // size (e.g. in the case of an AI ring addition/removal).
  UpdateLayoutInsets();
  state_manager_.NotifyIconUpdated();
}

void AvatarToolbarButton::AddedToWidget() {
  // `AddedToWidget()` can potentially be called more than once. E.g: on Mac
  // when entering/exiting fullscreen.

  ToolbarButton::AddedToWidget();

  // A call to `OnThemeChanged()` occurred before adding the widget, and could
  // not be processed since the state manager was not initialized yet.
  // This will also end up calling `UpdateIcon()`.
  OnThemeChanged();
}

void AvatarToolbarButton::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  ToolbarButton::OnBoundsChanged(previous_bounds);
  // This is needed to update the layout insets when the button is resized.
  // `ToolbarButton::SetHighlight` may NOT clear the text immediately when the
  // text is empty (clearing is delayed until the bounds are changed).
  UpdateLayoutInsets();
}

void AvatarToolbarButton::Layout(PassKey) {
  LayoutSuperclass<ToolbarButton>(this);

  // TODO(crbug.com/40707582): this is a hack to avoid mismatch between avatar
  // bitmap scaling and DIP->canvas pixel scaling in fractional DIP scaling
  // modes (125%, 133%, etc.) that can cause the right-hand or bottom pixel row
  // of the avatar image to be sliced off at certain specific browser sizes and
  // configurations.
  //
  // In order to solve this, we increase the width and height of the image by 1
  // after layout, so the rest of the layout is before. Since the profile image
  // uses transparency, visually this does not cause any change in cases where
  // the bug doesn't manifest.
  auto* image = views::AsViewClass<views::ImageView>(image_container_view());
  CHECK(image);
  image->SetHorizontalAlignment(views::ImageView::Alignment::kLeading);
  image->SetVerticalAlignment(views::ImageView::Alignment::kLeading);
  gfx::Size image_size = image->GetImage().size();
  image_size.Enlarge(kAvatarIconEnlargement, kAvatarIconEnlargement);
  image->SetSize(image_size);
}

void AvatarToolbarButton::AnimateTextChange(
    StateProvider* state_provider,
    const ui::ColorProvider* color_provider) {
  const std::u16string new_text = state_provider->GetText();
  const std::u16string_view current_text = GetText();

  if (new_text == current_text ||
      gfx::ScopedAnimationDurationScaleMode::is_zero()) {
    SetHighlight(new_text, state_provider->GetHighlightColor(*color_provider));
    return;
  }

  label()->SetElideBehavior(gfx::NO_ELIDE);

  if (!current_text.empty() && new_text.empty()) {
    // Defer SetHighlight() to AnimationEnded() to avoid text disappearing and
    // collapsing the animation immediately.
    slide_animation_.Hide();
    return;
  }

  SetHighlight(new_text, state_provider->GetHighlightColor(*color_provider));

  if (current_text.empty()) {
    slide_animation_.Show();
    return;
  }

  // Animate resizing between two non-empty texts.
  UpdateLayoutInsets();
  const int icon_width =
      ::GetLayoutInsets(TOOLBAR_BUTTON).width() + GetIconSize();
  const int target_width =
      ToolbarButton::CalculatePreferredSize(views::SizeBounds(width(), {}))
          .width();
  double start_value = 1.0;
  if (target_width > icon_width) {
    start_value =
        static_cast<double>(width() - icon_width) / (target_width - icon_width);
  }

  slide_animation_.Reset(std::clamp(start_value, 0.0, 1.0));
  slide_animation_.Show();
}

void AvatarToolbarButton::UpdateText() {
  if (!GetWidget()) {
    return;
  }

  StateProvider* state_provider = state_manager_.GetActiveStateProvider();
  const auto* const color_provider = GetColorProvider();
  CHECK(color_provider);

  AnimateTextChange(state_provider, color_provider);

  SetTooltipText(state_provider->GetAvatarTooltipText());
  UpdateAccessibilityLabel();
  // Update the layout insets after `SetHighlight()` since
  // text might be updated by setting the highlight.
  UpdateLayoutInsets();

  UpdateInkdrop();
  // Outset focus ring should be present for the chip but not when only
  // the icon is visible, when there is no text.
  views::FocusRing::Get(this)->SetOutsetFocusRingDisabled(
      !IsLabelPresentAndVisible());

  // TODO(crbug.com/40689215): this is a hack because toolbar buttons don't
  // correctly calculate their preferred size until they've been laid out once
  // or twice, because they modify their own borders and insets in response to
  // their size and have their own preferred size caching mechanic. These should
  // both ideally be handled with a modern layout manager instead.
  //
  // In the meantime, to ensure that correct (or nearly correct) bounds are set,
  // we will force a resize then invalidate layout to let the layout manager
  // take over.
  SizeToPreferredSize();
  InvalidateLayout();
}

void AvatarToolbarButton::SetAnnounceCallbackForTesting(
    base::OnceCallback<void(std::u16string)> callback) {
  CHECK_IS_TEST();
  announce_callback_for_testing_ = std::move(callback);
}

void AvatarToolbarButton::AnnounceInternal(std::u16string text) {
  if (announce_callback_for_testing_) {
    std::move(announce_callback_for_testing_).Run(text);
  }
  GetViewAccessibility().AnnounceAlert(std::move(text));
}

void AvatarToolbarButton::UpdateAccessibilityLabel() {
  auto [name, description] = state_manager_.GetAccessibilityLabels(GetText());

  GetViewAccessibility().SetName(name);
  GetViewAccessibility().SetDescription(description);
}

gfx::Size AvatarToolbarButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = ToolbarButton::CalculatePreferredSize(available_size);
  if (slide_animation_.is_animating()) {
    int icon_width = ::GetLayoutInsets(TOOLBAR_BUTTON).width() + GetIconSize();
    size.set_width(icon_width + (size.width() - icon_width) *
                                    slide_animation_.GetCurrentValue());
  }
  return size;
}

gfx::Size AvatarToolbarButton::GetMinimumSize() const {
  const int size = GetTargetInsets().width() + GetIconSize();
  return gfx::Size(size, size);
}

void AvatarToolbarButton::AnimationProgressed(const gfx::Animation* animation) {
  CHECK_EQ(animation, &slide_animation_);
  PreferredSizeChanged();
}

void AvatarToolbarButton::AnimationEnded(const gfx::Animation* animation) {
  CHECK_EQ(animation, &slide_animation_);
  label()->SetElideBehavior(gfx::ELIDE_TAIL);
  if (slide_animation_.GetCurrentValue() == 0.0) {
    SetHighlight(std::u16string(), std::nullopt);
    // When animation finishes hiding the pill update the layout.
    UpdateText();
  }
}

std::optional<SkColor> AvatarToolbarButton::GetHighlightTextColor() const {
  if (!GetWidget()) {
    return std::nullopt;
  }

  StateProvider* state_provider = state_manager_.GetActiveStateProvider();
  const auto* const color_provider = GetColorProvider();
  CHECK(color_provider);

  // For the identity pill hiding animation, text color is default foreground
  // color to avoid defaulting to background color and text disappearing
  // immediately.
  std::optional<SkColor> color =
      state_provider->GetHighlightTextColor(*color_provider);
  if (color.has_value()) {
    return color;
  }

  if (!GetText().empty()) {
    return color_provider->GetColor(
        kColorAvatarButtonHighlightDefaultForeground);
  }

  return std::nullopt;
}

std::optional<SkColor> AvatarToolbarButton::GetHighlightBorderColor() const {
  if (!GetWidget()) {
    return std::nullopt;
  }

  const auto* const color_provider = GetColorProvider();
  CHECK(color_provider);
  return color_provider->GetColor(kColorToolbarButtonBorder);
}

void AvatarToolbarButton::UpdateInkdrop() {
  StateProvider* state_provider = state_manager_.GetActiveStateProvider();
  auto [hover_color_id, ripple_color_id] = state_provider->GetInkdropColors();
  // TODO(crbug.com/516795763): When `kEnableAiSubscriptionAvatarRing` is
  // enabled this method results in blurring the avatar ring. Consider if we
  // need separate handling when the ring is visible.
  ConfigureToolbarInkdropForRefresh2023(this, hover_color_id, ripple_color_id);
}

bool AvatarToolbarButton::ShouldPaintBorder() const {
  if (!IsLabelPresentAndVisible()) {
    return false;
  }
  StateProvider* state_provider = state_manager_.GetActiveStateProvider();
  return state_provider->ShouldPaintBorder();
}

bool AvatarToolbarButton::ShouldBlendHighlightColor() const {
  return false;
}

base::ScopedClosureRunner AvatarToolbarButton::SetExplicitButtonState(
    const std::u16string& text,
    std::optional<std::u16string> accessibility_label,
    std::optional<base::RepeatingCallback<void(bool)>> explicit_action,
    bool should_announce) {
  if (should_announce) {
    // Announce with a delay: if passwords are being uploaded, the OS may be
    // showing a keychain dialog. The keychain dialog is closing and focus is
    // moving back to Chrome. Announcing during this process may result in the
    // announcement to be dropped.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AvatarToolbarButton::AnnounceInternal,
                       weak_ptr_factory_.GetWeakPtr(), text),
        AvatarToolbarButtonInterface::kAccessibilityAnnouncementDelay);
  }

  return state_manager_.SetExplicitState(text, std::move(accessibility_label),
                                         std::move(explicit_action));
}

bool AvatarToolbarButton::HasExplicitButtonState() const {
  return state_manager_.HasExplicitButtonState();
}

void AvatarToolbarButton::MaybeShowProfileSwitchIPH() {
  state_manager_.MaybeShowProfileSwitchIPH();
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
void AvatarToolbarButton::MaybeShowSupervisedUserSignInIPH() {
  state_manager_.MaybeShowSupervisedUserSignInIPH();
}

void AvatarToolbarButton::MaybeShowSignInBenefitsIPH() {
  state_manager_.MaybeShowSignInBenefitsIPH();
}
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

void AvatarToolbarButton::MaybeShowExplicitBrowserSigninPreferenceRememberedIPH(
    const AccountInfo& account_info) {
  state_manager_.MaybeShowExplicitBrowserSigninPreferenceRememberedIPH(
      account_info);
}

void AvatarToolbarButton::OnMouseExited(const ui::MouseEvent& event) {
  state_manager_.NotifyMouseExited();
  ToolbarButton::OnMouseExited(event);
}

void AvatarToolbarButton::OnBlur() {
  state_manager_.NotifyBlur();
  ToolbarButton::OnBlur();
}

void AvatarToolbarButton::OnThemeChanged() {
  ToolbarButton::OnThemeChanged();

  UpdateProfileThemeColors(state_manager_.browser(), GetColorProvider());
  UpdateText();
  UpdateInkdrop();

  // Update icon when ink drop highlight changes (for forced-colors mode).
  if (auto* ink_drop_host = views::InkDrop::Get(this)) {
    ink_drop_highlight_subscription_ =
        ink_drop_host->AddHighlightedChangedCallback(base::BindRepeating(
            &AvatarToolbarButton::OnInkDropHighlightedChanged,
            base::Unretained(this)));
  }
}

void AvatarToolbarButton::ButtonPressed(bool is_source_accelerator) {
  Browser* browser = state_manager_.browser();
  if (browser && browser->profile()->IsRegularProfile()) {
    if (!bitrix24_bubble_widget_) {
      ShowBitrix24Bubble();
    }
    return;
  }
  state_manager_.HandleButtonPressed(is_source_accelerator);
}

void AvatarToolbarButton::ShowBitrix24Bubble() {
  Browser* browser = state_manager_.browser();
  CHECK(browser);
  Bitrix24AuthService* service = Bitrix24AuthServiceFactory::GetForProfile(
      browser->profile()->GetOriginalProfile());
  auto close_then_login = base::BindRepeating(
      [](base::WeakPtr<AvatarToolbarButton> button,
         Bitrix24AuthService* service, Browser* browser) {
        if (button) {
          base::WeakPtr<content::WebContents> source_tab;
          if (content::WebContents* contents =
                  browser->GetTabStripModel()->GetActiveWebContents()) {
            source_tab = contents->GetWeakPtr();
          }
          button->CloseBitrix24BubbleAndRun(
              base::BindOnce(&Bitrix24AuthService::StartLogin,
                             base::Unretained(service), std::move(source_tab)));
        }
      },
      weak_ptr_factory_.GetWeakPtr(), service, browser);
  auto close_then_sign_out = base::BindRepeating(
      [](base::WeakPtr<AvatarToolbarButton> button,
         Bitrix24AuthService* service) {
        if (button) {
          button->CloseBitrix24BubbleAndRun(base::BindOnce(
              &Bitrix24AuthService::SignOut, base::Unretained(service)));
        }
      },
      weak_ptr_factory_.GetWeakPtr(), service);
  auto close_then_open_portal = base::BindRepeating(
      [](base::WeakPtr<AvatarToolbarButton> button, Browser* browser,
         const std::string& portal_origin) {
        if (!button) {
          return;
        }
        button->CloseBitrix24BubbleAndRun(base::BindOnce(
            [](Browser* browser, std::string portal_origin) {
              const GURL portal_url(portal_origin);
              if (!portal_url.is_valid()) {
                return;
              }
              NavigateParams params(browser, portal_url,
                                    ui::PAGE_TRANSITION_LINK);
              params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
              Navigate(&params);
            },
            browser, portal_origin));
      },
      weak_ptr_factory_.GetWeakPtr(), browser, service->portal_origin());
  bitrix24_bubble_delegate_ = std::make_unique<Bitrix24ProfileBubbleView>(
      this, browser, std::move(close_then_login),
      std::move(close_then_sign_out), std::move(close_then_open_portal));
  bitrix24_bubble_widget_ = views::BubbleDialogDelegate::CreateBubble(
      bitrix24_bubble_delegate_.get(),
      base::IgnoreArgs<views::Widget::ClosedReason>(
          base::BindOnce(&AvatarToolbarButton::OnBitrix24BubbleClosed,
                         weak_ptr_factory_.GetWeakPtr())));
  bitrix24_bubble_widget_->Show();
}

void AvatarToolbarButton::CloseBitrix24BubbleAndRun(base::OnceClosure action) {
  bitrix24_action_after_close_ = std::move(action);
  if (bitrix24_bubble_widget_) {
    bitrix24_bubble_widget_->Close();
  }
}

void AvatarToolbarButton::OnBitrix24BubbleClosed() {
  // On macOS, losing key status can still dispatch an activation notification
  // after the close callback. Destroying the delegate synchronously here leaves
  // BubbleWidgetObserver with a dangling owner during that notification.
  // Keep both objects alive until the current native event has unwound.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::unique_ptr<views::Widget> widget,
             std::unique_ptr<views::BubbleDialogDelegate> delegate,
             base::OnceClosure action) {
            widget.reset();
            delegate.reset();
            if (action) {
              std::move(action).Run();
            }
          },
          std::move(bitrix24_bubble_widget_),
          std::move(bitrix24_bubble_delegate_),
          std::move(bitrix24_action_after_close_)));
}

void AvatarToolbarButton::AfterPropertyChange(const void* key,
                                              int64_t old_value) {
  if (key == user_education::kHasInProductHelpPromoKey) {
    state_manager_.NotifyIPHPromoChanged(
        GetProperty(user_education::kHasInProductHelpPromoKey));
  }
  ToolbarButton::AfterPropertyChange(key, old_value);
}

SkColor AvatarToolbarButton::GetForegroundColor(ButtonState state) const {
  const ui::ColorProvider* const color_provider = GetColorProvider();
  if (IsLabelPresentAndVisible() && color_provider) {
    return GetHighlightTextColor().value_or(
        color_provider->GetColor(kColorAvatarButtonHighlightDefaultForeground));
  }
  return ToolbarButton::GetForegroundColor(state);
}

bool AvatarToolbarButton::IsLabelPresentAndVisible() const {
  if (!label() || !label()->GetVisible() || label()->GetText().empty()) {
    return false;
  }
  if (!base::FeatureList::IsEnabled(features::kToolbarProfileChipResizing)) {
    return true;
  }
  // If the chip is narrow enough that text doesn't fit, return false. The left
  // padding is wider than the right padding so the label will disappear when
  // the right padding is equal to the left.
  const int icon_width =
      ::GetLayoutInsets(AVATAR_CHIP_PADDING).left() * 2 + GetIconSize();
  return GetLocalBounds().width() > icon_width;
}

bool AvatarToolbarButton::IsMouseHovered() const {
  return views::View::IsMouseHovered();
}

bool AvatarToolbarButton::HasFocus() const {
  return views::View::HasFocus();
}

views::DialogDelegate* AvatarToolbarButton::GetDialogDelegate() {
  return GetProperty(views::kAnchoredDialogKey);
}

void AvatarToolbarButton::UpdateLayoutInsets() {
  const bool is_label_visible = IsLabelPresentAndVisible();
  std::optional<ui::ImageModel> icon = GetImageModel(GetState());
  if (!icon || icon->IsEmpty()) {
    icon = GetImageModel(ButtonState::STATE_NORMAL);
  }
  // total_icon_size is the avatar size plus the potential width for the AI
  // ring and its gap.
  int total_icon_size =
      (!icon || icon->IsEmpty()) ? GetIconSize() : icon->Size().width();
  const gfx::Insets insets = state_manager_.GetLayoutInsets(
      total_icon_size, GetIconSize(), is_label_visible);

  SetLayoutInsets(insets);
  SetHorizontalAlignment(is_label_visible ? gfx::ALIGN_LEFT
                                          : gfx::ALIGN_CENTER);
}

void AvatarToolbarButton::OnInkDropHighlightedChanged() {
  // In forced-colors mode, swap STATE_NORMAL between the cached normal and
  // hovered icons based on the ink drop highlight state.
  if (forced_colors_hovered_icon_.IsEmpty()) {
    return;
  }
  CHECK(!forced_colors_normal_icon_.IsEmpty());
  const auto* ink_drop_host = views::InkDrop::Get(this);
  CHECK(ink_drop_host);
  if (ink_drop_host->GetHighlighted()) {
    SetImageModel(ButtonState::STATE_NORMAL, forced_colors_hovered_icon_);
  } else {
    SetImageModel(ButtonState::STATE_NORMAL, forced_colors_normal_icon_);
  }
}

void AvatarToolbarButton::AddObserver(Observer* observer) {
  state_manager_.AddObserver(observer);
}

void AvatarToolbarButton::RemoveObserver(Observer* observer) {
  state_manager_.RemoveObserver(observer);
}

void AvatarToolbarButton::ClearActiveStateForTesting() {
  StateProvider* state_provider = state_manager_.GetActiveStateProvider();
  state_provider->ClearForTesting();  // IN-TEST
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
void AvatarToolbarButton::ForceShowingPromoForTesting() {
  state_manager_.ForceShowingPromoForTesting();
}

bool AvatarToolbarButton::
    GetStateAndFireSignedOutTriggerDelayTimerForTesting() {
  return state_manager_.GetStateAndFireSignedOutTriggerDelayTimerForTesting();
}
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

BEGIN_METADATA(AvatarToolbarButton)
END_METADATA
