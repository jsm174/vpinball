// license:GPLv3+

#pragma once

namespace VPX::Layers
{

// The window of the overlay layers: the ball heatmap (show, layer, clear) and the play bookmarks. Drawn once per frame by the editor;
// nothing is shown while there is neither a heatmap nor a bookmark.
void RenderPanel(float dpi);

}
