"""Compose transparent UI art from paired UE scene captures.

The RGB capture uses Final Color (LDR). The mask capture uses Scene Color
(HDR), whose alpha is *inverse* opacity in UE 5.7. Both pairs are generated
from the actual weapon meshes by the editor-only capture script.
"""

from pathlib import Path

from PIL import Image, ImageChops


PROJECT = Path(__file__).resolve().parents[1]
CAPTURES = PROJECT / "Saved" / "CodexTemp" / "ShopRenders"
SOURCE_ART = PROJECT / "SourceArt" / "ShopIcons"
CANVAS = (1024, 576)
FIT_BOX = (890, 474)

ITEMS = (
    ("Pistol", "T_Shop_UpgradedPistol.png", True),
    ("Rifle", "T_Shop_Rifle.png", True),
    ("Sniper", "T_Shop_Sniper.png", False),
)


def validate_real_material(name: str, rgb: Image.Image, inverse_alpha: Image.Image) -> None:
    """Reject the flat default shader that the editor can render while compiling.

    The actual meshes here are deliberately white/black/gray, so generic color
    saturation is not a useful test for the pistol or rifle. These conservative
    signatures were measured against the project's real source materials.
    """
    pixels = list(rgb.get_flattened_data())
    opaque = [pixel for pixel, mask in zip(pixels, inverse_alpha.getchannel("A").get_flattened_data())
              if mask < 128]
    if not opaque:
        raise ValueError(f"{name}: capture contains no opaque weapon pixels")
    dark_fraction = sum(sum(pixel) / 3 < 100 for pixel in opaque) / len(opaque)
    mean_brightness = sum(sum(pixel) / 3 for pixel in opaque) / len(opaque)
    colorful_fraction = sum(max(pixel) - min(pixel) > 15 for pixel in opaque) / len(opaque)
    if name == "Pistol" and dark_fraction < 0.27:
        raise ValueError(f"{name}: dark grip absent; material may still be compiling")
    if name == "Rifle" and mean_brightness > 120:
        raise ValueError(f"{name}: gray receiver absent; material may still be compiling")
    if name == "Sniper" and colorful_fraction < 0.06:
        raise ValueError(f"{name}: gold accents absent; material may still be compiling")
    print(f"{name}: dark={dark_fraction:.3f} brightness={mean_brightness:.1f} "
          f"colorful={colorful_fraction:.3f}")


def compose(name: str, filename: str, flip: bool) -> tuple[Path, Image.Image, str]:
    rgb_path = CAPTURES / f"{name}_RGB.png"
    mask_path = CAPTURES / f"{name}_InvAlpha.png"
    with Image.open(rgb_path) as rgb_src, Image.open(mask_path) as mask_src:
        if rgb_src.size != CANVAS or mask_src.size != CANVAS:
            raise ValueError(f"{name}: capture sizes do not match {CANVAS}")
        rgb = rgb_src.convert("RGB")
        mask = mask_src.convert("RGBA")
        validate_real_material(name, rgb, mask)
        alpha = ImageChops.invert(mask.getchannel("A"))

    bounds = alpha.getbbox()
    if not bounds:
        raise ValueError(f"{name}: capture contains no visible weapon")
    if bounds[0] <= 1 or bounds[1] <= 1 or bounds[2] >= CANVAS[0] - 1 or bounds[3] >= CANVAS[1] - 1:
        raise ValueError(f"{name}: weapon touches frame edge: {bounds}; recapture with more room")

    captured = rgb.convert("RGBA")
    captured.putalpha(alpha)
    crop = captured.crop(bounds)
    scale = min(FIT_BOX[0] / crop.width, FIT_BOX[1] / crop.height)
    size = (round(crop.width * scale), round(crop.height * scale))
    crop = crop.resize(size, Image.Resampling.LANCZOS)
    if flip:
        crop = crop.transpose(Image.Transpose.FLIP_LEFT_RIGHT)

    output = Image.new("RGBA", CANVAS, (0, 0, 0, 0))
    output.alpha_composite(crop, ((CANVAS[0] - size[0]) // 2, (CANVAS[1] - size[1]) // 2))
    destination = SOURCE_ART / filename
    alpha_extent = output.getchannel("A").getextrema()
    description = f"{name}: source={bounds} final={size} alpha={alpha_extent} -> {destination}"
    return destination, output, description


if __name__ == "__main__":
    SOURCE_ART.mkdir(parents=True, exist_ok=True)
    prepared = [compose(*item) for item in ITEMS]
    for destination, output, description in prepared:
        output.save(destination, optimize=True)
        print(description)
