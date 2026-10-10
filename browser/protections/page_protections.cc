// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/page_protections.h"

#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace ghost::protections {

PageProtections::PageProtections(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<PageProtections>(*contents) {}

PageProtections::~PageProtections() = default;

// static
void PageProtections::RecordBlocked(content::GlobalRenderFrameHostId id) {
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(id);
  if (!frame) {
    return;
  }
  content::RenderFrameHost* main = frame->GetOutermostMainFrame();
  if (!main->GetPage().IsPrimary()) {
    return;
  }
  content::WebContents* contents = content::WebContents::FromRenderFrameHost(main);
  CreateForWebContents(contents);
  PageProtections* protections = FromWebContents(contents);
  protections->SetCount(protections->blocked_count_ + 1);
}

void PageProtections::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PageProtections::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void PageProtections::PrimaryPageChanged(content::Page& page) {
  SetCount(0);
}

void PageProtections::SetCount(int count) {
  blocked_count_ = count;
  for (Observer& observer : observers_) {
    observer.OnBlockedCountChanged();
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PageProtections);

}  // namespace ghost::protections
