// license:GPLv3+

#pragma once

namespace VPX::Proposals
{

// The window listing the pending proposals with Accept / Reject and a show / hide toggle for each ghost. Drawn once per frame by the editor; nothing is
// shown while there are no pending proposals.
void RenderPanel(float dpi);

}
