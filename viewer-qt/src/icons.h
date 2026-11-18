// icons - Mastercam's images, read at run time from the icon pack the user builds from their own installation
// (tools/setup_from_install.py / extract_icons.py -> <config dir>/icons.pack; MCAMUI_ICON_PACK = another file,
// MCAMUI_ICONS = a folder of PNGs for development). Which image belongs to which command comes from ui/icon_map.tsv
// (tools/gen_icon_map.py, from the BCGP ribbon XML). Nothing is bundled: without the pack every icon is the generated
// letter tile of viewer/mcamui.c draw_icon. Names as in mcamui.c icon(): a command label, "gallery|item[#n]",
// "#file:PNG", "#strip:PNG:cell" (16 px cells), "#strip14:PNG:cell" (14 px cells).
#pragma once
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace Icons {
// the icon for NAME; px = the drawn size (16 small, 32 large, 64x32 gallery: w x h)
QIcon icon(const QString &name, int w = 16, int h = 16);
bool hasImage(const QString &name);            // an image from the pack (not the letter tile)
QPixmap tile(const QString &name, int sz);       // the generated tile (mcamui draw_icon, no image)
bool packLoaded();
QPixmap image(const QString &name);             // the image at its own size (mcamui pp_pic: statics sized to the bitmap); null = none
}
