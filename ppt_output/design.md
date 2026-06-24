# Design Document

## 1. Profile Baseline Declaration

- **Profile selection**: `profiles/strategic.md`
- **Selection rationale**: This is a competition project pitch (全国大学生嵌入式芯片与系统设计竞赛), which aligns with strategic planning presentations — persuading judges to recognize the project's innovation and feasibility.
- **Referenced dimensions**: Narrative structure (Problem → Solution → Demo → Innovation), information density (medium-high), key-point prominence, data persuasiveness, and premium restraint.
- **Deviation notes**:
  - The user explicitly requests "深色科技感背景" (dark tech background), so we deviate from the light-background default.
  - This is a technical project, not a business plan, so financial/ROI data is replaced with technical specs and demo scenarios.
  - The audience is competition judges (professors + industry experts), so technical depth is retained but made accessible.

## 2. Style Baseline Declaration

- **Style anchor selection**:
  1. **Apple Keynote (dark mode)**: Referenced for its dark background + high-contrast typography + clean data presentation style. The dark canvas makes key numbers and text pop with authority.
  2. **Swiss International Style**: Referenced for its grid-based layouts, strict alignment, and typographic hierarchy. Ensures the technical content feels organized and credible.
  3. **Cyberpunk/Tech aesthetic (restrained)**: Referenced for the dark tech atmosphere with neon-like accent colors (emerald green, amber gold) to evoke the "embedded chip / AI" theme without becoming garish.
- **Referenced dimension explanation**:
  - From Apple: Dark background treatment, large bold numbers, minimal decoration, focus on content.
  - From Swiss Style: Grid layouts, sharp-cornered containers, consistent margins, alignment discipline.
  - From Tech aesthetic: Color palette (deep space blue-black + chip green + amber gold), subtle glow effects on accent elements.

## 3. Style Details

### 1. Color Design Principles
- **Overall tendency**: Striking & bold (for a competition pitch) but with premium restraint. The dark background creates instant visual impact; the green accent signals "embedded technology / AI".
- **Temperature**: Cool-dark foundation with warm accent highlights.
- **Primary color**: `#00D4AA` (emerald green, reminiscent of circuit boards and chip LEDs, not the cliché blue).
- **Background**: `#0B0F19` (deep space blue-black, nearly black with a hint of blue).
- **Surface**: `#111827` (slightly lighter dark gray for cards and containers).
- **Text (primary)**: `#F8FAFC` (off-white, easy on the eyes against dark).
- **Text (secondary)**: `#94A3B8` (cool gray for subtitles, annotations, body text).
- **Accent**: `#F59E0B` (amber gold, used very sparingly for key numbers, CTAs, and highlights). It contrasts strongly with the dark background without clashing with the green primary.
- **Muted**: `#334155` (slate gray for borders, dividers, subtle decorations).

### 2. Font Usage Principles
- **Chinese title font**: `alimamashuheiti` (geometric sans-serif, strong commercial/tech feel, clean and structured).
- **Chinese body font**: `MiSans` (modern, excellent screen rendering, highly readable).
- **English title font**: `Liter` (modern neo-grotesque, clean and rational, perfect for tech).
- **English body font**: `Liter` (consistent with titles, or `QuattrocentoSans` for softer body text).
- **Font size hierarchy**:
  - Cover title: 48px (alimamashuheiti, bold).
  - Cover subtitle: 22px (MiSans).
  - Page title: 32px (alimamashuheiti).
  - Section subtitle: 24px (MiSans).
  - Body text: 18-20px (MiSans, lineHeight 1.6).
  - Big numbers / KPIs: 48-56px (Liter, bold).
  - Annotations / sources: 14px (MiSans, secondary color).
  - Navigation / page numbers: 12px (Liter).

### 3. Text Box and Container Styles
- **Content separation**: Primarily whitespace + font size hierarchy. Cards are used when necessary but kept sharp-cornered (rect, not roundRect).
- **Cards**: Sharp-cornered rectangles (`rect`), filled with `#111827` or transparent with a `#334155` 1px border. No rounded corners (per strategic profile prohibition).
- **Decorative elements**:
  - Thin horizontal lines (`straightConnector1`) in `#00D4AA` or `#334155` as section dividers.
  - Small square or rectangle shapes in primary color as bullet markers or accent bars.
  - Subtle gradient overlays on cover/chapter pages using `gradient` fills (dark to transparent).

### 4. Image Style
- **Icons**: Font Awesome solid icons (`fas:`), used in primary color (`#00D4AA`) or white. Icons are used to anchor key points (e.g., a chip icon for hardware, a robot icon for AI). Usage is encouraged but restrained — one icon per key point, not decorative clutter.
- **Tables**: Minimal tech style. Dark header (`#00D4AA` background, white text), alternating dark body rows (`#111827`, `#0B0F19`), thin `#334155` borders.
- **Charts**: Minimal flat style. Series colors use the primary palette (`#00D4AA`, `#F59E0B`, `#3B82F6` only if needed). No 3D, no heavy shadows. Grid lines in `#334155`.
- **Illustrations**: Since we lack real product photos, we rely on high-quality icons, geometric shapes, and clean typography to create visual interest. If images are used, they should be dark-toned tech photos with a gradient mask overlay to blend into the background.

## 4. Layout System

### 1. Global Layout Characteristics
- **Page size**: 1280 x 720 (16:9).
- **Page margins**: Left 60px, Right 60px, Top 50px, Bottom 40px. This creates a contained frame within the dark canvas.
- **Unified page elements**:
  - A thin horizontal line at y=680 (spanning 1160px from x=60) as a subtle footer separator, with a small page number or short tagline below it.
  - Optionally, a small accent bar (4px wide, 40px tall, `#00D4AA`) at the top-left corner of content pages as a brand marker.
- **Grid**: All elements align to a 60px left margin and 60px right margin. Content blocks should have consistent widths and align perfectly.

### 2. Special Page Layouts
- **Cover page**: Hero design. Full-bleed dark background with a subtle gradient overlay (from `#0B0F19` to transparent). Centered or left-aligned large title text. A small accent line under the subtitle. Bottom section for competition info (small, secondary color).
- **Table of contents**: Not included in the original 12-page script, but if needed, use a grid layout with numbered chapters in equal-width columns.
- **Chapter transition pages**: Not strictly needed for 12 pages, but if a "演示" section divider is desired, use a dark page with a large chapter number (e.g., "03") in oversized dimmed text (`#1E293B`) behind the actual chapter title in white.
- **Final page**: Centered "Thank you" in large typography, with a subtle gradient overlay and contact info below.

### 3. Content Page Layout Patterns
- **Pattern A (Left-Right Split)**: Title at top. Left side: text + bullets (55% width). Right side: icon cluster, shape diagram, or image (45% width). Used for: 痛点, 方案, 技术原理.
- **Pattern B (Top-Bottom Sections)**: Title at top. Middle: 3-4 equal-width cards in a row. Bottom: annotation. Used for: 核心创新点, 应用场景, 硬件一览.
- **Pattern C (Big Numbers)**: Title at top. Center: 2-3 oversized numbers with labels. Bottom: supporting text. Used for: 开发状态 (progress metrics), 总结 (key stats).
- **Pattern D (Demo/Showcase)**: Title at top. Large central area for a diagram or screenshot placeholder. Bottom: scenario description. Used for: 演示①, 演示②.
- **Pattern E (Full-width list)**: Title at top. Full-width structured list with icons and text. Used for: 硬件一览, 开发状态.

## 5. Style Usage Rules
- **textStyle `$title`**: Cover title, page titles. alimamashuheiti, 32-48px, white or primary green.
- **textStyle `$subtitle`**: Cover subtitle, section headers. MiSans, 22-24px, secondary gray.
- **textStyle `$body`**: Body text, bullet points. MiSans, 18-20px, off-white, lineHeight 1.6.
- **textStyle `$caption`**: Annotations, sources, page numbers. MiSans, 12-14px, secondary gray.
- **textStyle `$kpi`**: Big numbers, key stats. Liter, 48-56px, primary green or amber gold.
- **color `$primary`**: `#00D4AA` — titles, accent bars, icons, key highlights, chart primary series.
- **color `$secondary`**: `#94A3B8` — subtitles, body text, annotations.
- **color `$accent`**: `#F59E0B` — key numbers, CTAs, secondary highlights, important badges.
- **color `$background`**: `#0B0F19` — page background.
- **color `$surface`**: `#111827` — card backgrounds, table body rows.
- **color `$muted`**: `#334155` — borders, dividers, subtle decorations.
- **tableStyle `$default`**: Dark tech style. Header: primary green fill, white bold text. Body: alternating surface/background, white text, muted borders.

## 6. Risk Prohibitions
- [ ] **Color**: Do NOT use bright blue (`#2C80FD`, `#0A97C0`) as primary or accent — this is the most cliché tech color and conflicts with the user's "dark tech" request. Stick to the green/amber palette.
- [ ] **Color**: Do NOT use white (`#FFFFFF`) as the page background. The canvas must remain dark (`#0B0F19`).
- [ ] **Color**: Do NOT use more than 3 main colors (green, amber, dark gray). Avoid rainbow palettes.
- [ ] **Layout**: Do NOT use rounded rectangles (`roundRect`) for cards or containers. Use sharp `rect` shapes only.
- [ ] **Layout**: Do NOT leave bottom-right empty in left-right splits. Ensure both sides have balanced visual weight.
- [ ] **Layout**: Do NOT misalign cards in multi-column layouts. All card tops and bottoms must align perfectly.
- [ ] **Decoration**: Do NOT use generic stock photos. Use icons, shapes, and clean typography instead.
- [ ] **Decoration**: Do NOT use heavy drop shadows or 3D effects. Keep it flat and minimal.
- [ ] **Font size**: Body text must NOT go below 18px. Annotations must NOT go below 12px. Page titles must NOT go below 28px.
- [ ] **Content**: Do NOT write long paragraphs. Keep bullets concise (under 15 words per line).
- [ ] **Content**: Do NOT use vague language like "very good", "excellent". Use concrete technical specs and data.

## 7. Theme Definition

```yaml
theme:
  colors:
    primary: "#00D4AA"
    secondary: "#94A3B8"
    accent: "#F59E0B"
    background: "#0B0F19"
    surface: "#111827"
    text: "#F8FAFC"
    muted: "#334155"
  textStyles:
    title:
      fontSize: 32
      color: "$text"
      fontFamily: "Liter, alimamashuheiti"
    coverTitle:
      fontSize: 48
      color: "$text"
      fontFamily: "Liter, alimamashuheiti"
    subtitle:
      fontSize: 22
      color: "$secondary"
      fontFamily: "Liter, MiSans"
    body:
      fontSize: 18
      color: "$text"
      fontFamily: "Liter, MiSans"
      lineHeight: 1.6
    caption:
      fontSize: 14
      color: "$secondary"
      fontFamily: "Liter, MiSans"
    kpi:
      fontSize: 48
      color: "$primary"
      fontFamily: "Liter, alimamashuheiti"
    kpiAccent:
      fontSize: 48
      color: "$accent"
      fontFamily: "Liter, alimamashuheiti"
  tableStyles:
    default:
      fontSize: 16
      fontFamily: "Liter, MiSans"
      headerFill: "$primary"
      headerColor: "#000000"
      headerBold: true
      bodyFill: ["$surface", "$background"]
      bodyColor: "$text"
      border:
        style: solid
        width: 1
        color: "$muted"
```