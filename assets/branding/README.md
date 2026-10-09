# CamCord mark

Coral C-shaped capture frame, ivory recording dot, charcoal app tile. The current production master is `camcord-logo-taskbar.png`, edited with the built-in image-generation tool to enlarge the mark and reduce padding for Windows taskbar visibility. The original `camcord-logo.png` is preserved. Transparency is preserved outside the tile. Assets are covered by the project's MIT license.

Run `scripts/generate-brand-icons.ps1` from the repository to package the original into the committed Windows icon (16, 20, 24, 32, 48, 64, 128 and 256 px), web header image and favicon. The app, shortcuts and installer share this mark. Derived files are committed so building CamCord does not require image generation.

## Generation prompt

```text
Use case: logo-brand
Asset type: production app icon and header logo for CamCord, a lightweight Windows screen recorder.
Primary request: Design one distinctive, premium, beautifully balanced CamCord symbol: a bold geometric capital C shaped like a rounded screen-capture frame, with a single warm-white recording dot nestled inside its open right side. The C is coral red #f2675f; the recording dot is warm ivory #f5f2ed. Both sit on a deep charcoal #151922 rounded-square app tile. Harmonize with a dark charcoal, coral and ivory recording interface using Space Grotesk typography. The mark should feel precise, friendly and modern, not a generic video camera pictogram.
Style/medium: exceptionally clean flat vector-like logo, two simple bold shapes with deliberate negative space. Uniform solid colors, smooth crisp edges, confident silhouette.
Composition/framing: one centered square icon only. Rounded-square charcoal tile occupies about 92% of the square canvas, narrow transparent margin outside it. Coral C and ivory recording dot occupy about 68% of the tile width, optically centered, substantial clear space between dot and C. Readable as a recognizable C even at 16px.
Constraints: genuinely transparent alpha outside the dark rounded-square tile. No text, no wordmark, no letters other than the symbolic C, no tagline, no extra symbols. No mockup, no presentation board, no shadows, no gradients, no glow, no texture, no 3D, no watermark. Original design, not an imitation of an existing logo.
```

## Taskbar optical-size correction

The production master enlarges the visible C and dot from roughly 60% to 80% of the canvas. No Windows taskbar settings are changed. Source, Windows icon, header and favicon stay in sync.

Built-in image-generation edit prompt:

```text
Use case: precise-object-edit / logo-brand.
Image 1 is the edit target: the existing CamCord app logo.
Change only the visual scale and padding, preserving the existing identity exactly: coral C-shaped rounded capture frame, single ivory recording dot, dark charcoal rounded square tile. This logo currently looks too small in a Windows taskbar because its C and dot occupy only about 60% of the canvas and its dark tile blends into the taskbar.
Make the coral C and ivory dot together occupy 85% of the full square canvas width and the C occupy 85% of its height, with only about 7% inset between the glyph and tile edge. Enlarge the complete C/dot group proportionately, retaining the C's thick rounded ends, existing spacing and dot on the open right side. The dark rounded-square tile should occupy 98% of the canvas, with only a 1% transparent outer margin; preserve corner rounding and genuine transparent alpha outside it. Keep coral #f2675f, ivory #f5f2ed and charcoal #151922. Strong, crisp edges and unmistakable silhouette at 16, 20, 24 and 32 pixels. No text, no extra shapes, no change in concept, no mockup, no comparison panel, no added shadows, no glow. Output one square transparent app-icon asset only.
```

