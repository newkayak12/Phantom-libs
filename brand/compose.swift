import AppKit
// usage: compose <mode> <src.png> <out> <size>
//   icon | head | splash: <out.png>, <size> is the width
//   ico:    <out.ico>, <size> is a comma-separated list of sizes (head crop up to 32 px, icon above)
//   logo | header: <out.bmp> for the Windows installer wizard (164x314 / 150x57), <size> is ignored
let a = CommandLine.arguments
let mode = a[1], src = NSImage(contentsOfFile: a[2])!, out = a[3], sizeArg = a[4]
var rect = NSRect(x: 0, y: 0, width: 1, height: 1)
guard let cg = src.cgImage(forProposedRect: &rect, context: nil, hints: nil) else { fatalError("load") }
let W = cg.width, H = cg.height
func bgColor() -> CGColor {
  let rep = NSBitmapImageRep(cgImage: cg); return (rep.colorAt(x: 8, y: 8) ?? .lightGray).cgColor
}
// draw the full image fitted to height, then fade its left/right edges into the background color
func drawFaded(_ ctx: CGContext, _ r: CGRect) {
  ctx.draw(cg, in: r)
  let fade = r.width * 0.12
  let cs = CGColorSpace(name: CGColorSpace.sRGB)!
  let c = bgColor(), clear = c.copy(alpha: 0)!
  let g = CGGradient(colorsSpace: cs, colors: [c, clear] as CFArray, locations: [0, 1])!
  ctx.saveGState(); ctx.clip(to: CGRect(x: r.minX, y: r.minY, width: fade, height: r.height))
  ctx.drawLinearGradient(g, start: CGPoint(x: r.minX, y: 0), end: CGPoint(x: r.minX + fade, y: 0), options: []); ctx.restoreGState()
  ctx.saveGState(); ctx.clip(to: CGRect(x: r.maxX - fade, y: r.minY, width: fade, height: r.height))
  ctx.drawLinearGradient(g, start: CGPoint(x: r.maxX, y: 0), end: CGPoint(x: r.maxX - fade, y: 0), options: []); ctx.restoreGState()
}
func image(_ w: Int, _ h: Int, _ draw: (CGContext) -> Void) -> CGImage {
  let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                      space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
  ctx.interpolationQuality = .high
  draw(ctx)
  return ctx.makeImage()!
}
func write(_ img: CGImage, _ type: NSBitmapImageRep.FileType) {
  try! NSBitmapImageRep(cgImage: img).representation(using: type, properties: [:])!.write(to: URL(fileURLWithPath: out))
}
func render(_ w: Int, _ h: Int, _ draw: (CGContext) -> Void) { write(image(w, h, draw), .png) }
// Square crops centered on the face (phantom-hood.png: the eyes sit at about 55% across, 52% down):
// icon shows the whole square art, head only the hood opening and eyes (for 16/32 px)
func iconImage(_ mode: String, _ size: Int) -> CGImage {
  let faceX = Int(Double(W) * 0.55), faceY = Int(Double(H) * 0.52)
  let side = mode == "icon" ? min(W, H) : Int(Double(W) * 0.58)
  let x0 = min(max(faceX - side / 2, 0), W - side), y0 = min(max(faceY - side / 2, 0), H - side)
  let crop = CGRect(x: x0, y: y0, width: side, height: side)
  let piece = cg.cropping(to: crop)!
  return image(size, size) { ctx in
    let s = CGFloat(size), inset = mode == "icon" ? s * 0.098 : 0, body = s - inset * 2
    let r = body * 0.225
    let path = CGPath(roundedRect: CGRect(x: inset, y: inset, width: body, height: body), cornerWidth: r, cornerHeight: r, transform: nil)
    ctx.addPath(path); ctx.clip()
    ctx.setFillColor(bgColor()); ctx.fill(CGRect(x: 0, y: 0, width: s, height: s))
    ctx.draw(piece, in: CGRect(x: inset, y: inset, width: body, height: body))
  }
}
func le<T: FixedWidthInteger>(_ v: T) -> [UInt8] { withUnsafeBytes(of: v.littleEndian) { Array($0) } }
// plain bottom-up 24-bit BMP (BITMAPINFOHEADER), the format NSIS expects for its wizard images
func writeBmp(_ img: CGImage) {
  let rep = NSBitmapImageRep(cgImage: img), w = img.width, h = img.height, stride = (w * 3 + 3) / 4 * 4
  var px: [UInt8] = []
  for y in (0..<h).reversed() {
    for x in 0..<w {
      let c = rep.colorAt(x: x, y: y)!.usingColorSpace(.sRGB)!
      px += [c.blueComponent, c.greenComponent, c.redComponent].map { UInt8(($0 * 255).rounded()) }
    }
    px += [UInt8](repeating: 0, count: stride - w * 3)
  }
  var d: [UInt8] = [0x42, 0x4D] + le(UInt32(54 + px.count)) + le(UInt32(0)) + le(UInt32(54))
  d += le(UInt32(40)) + le(Int32(w)) + le(Int32(h)) + le(UInt16(1)) + le(UInt16(24)) + le(UInt32(0))
  d += le(UInt32(px.count)) + le(Int32(2834)) + le(Int32(2834)) + le(UInt32(0)) + le(UInt32(0))
  try! Data(d + px).write(to: URL(fileURLWithPath: out))
}
// ICO with one 32-bit BMP entry per size (the format of the stock icons, which NSIS and the launcher generator read)
func writeIco(_ sizes: [Int]) {
  var entries: [[UInt8]] = []
  for s in sizes {
    let img = iconImage(s <= 32 ? "head" : "icon", s)
    let rep = NSBitmapImageRep(cgImage: img)
    var d = le(UInt32(40)) + le(Int32(s)) + le(Int32(s * 2)) + le(UInt16(1)) + le(UInt16(32)) + le(UInt32(0))
    d += le(UInt32(s * s * 4)) + le(Int32(0)) + le(Int32(0)) + le(UInt32(0)) + le(UInt32(0))
    for y in (0..<s).reversed() {  // bottom-up BGRA, straight alpha
      for x in 0..<s {
        let c = rep.colorAt(x: x, y: y)!.usingColorSpace(.sRGB)!
        d += [c.blueComponent, c.greenComponent, c.redComponent, c.alphaComponent].map { UInt8(($0 * 255).rounded()) }
      }
    }
    d += [UInt8](repeating: 0, count: ((s + 31) / 32) * 4 * s)  // AND mask, unused with alpha
    entries.append(d)
  }
  var ico = le(UInt16(0)) + le(UInt16(1)) + le(UInt16(sizes.count))
  var offset = 6 + 16 * sizes.count
  for (s, d) in zip(sizes, entries) {
    ico += [UInt8(s % 256), UInt8(s % 256), 0, 0] + le(UInt16(1)) + le(UInt16(32)) + le(UInt32(d.count)) + le(UInt32(offset))
    offset += d.count
  }
  for d in entries { ico += d }
  try! Data(ico).write(to: URL(fileURLWithPath: out))
}
switch mode {
case "icon", "head":
  write(iconImage(mode, Int(sizeArg)!), .png)
case "ico":
  writeIco(sizeArg.split(separator: ",").map { Int($0)! })
case "logo":
  // the installer's welcome/finish page side panel: the art on the background color, a bit above center
  writeBmp(image(164, 314) { ctx in
    ctx.setFillColor(bgColor()); ctx.fill(CGRect(x: 0, y: 0, width: 164, height: 314))
    drawFaded(ctx, CGRect(x: 0, y: 100, width: 164, height: 164))
  })
case "header":
  // the installer's page header, right-aligned
  writeBmp(image(150, 57) { ctx in
    ctx.setFillColor(bgColor()); ctx.fill(CGRect(x: 0, y: 0, width: 150, height: 57))
    drawFaded(ctx, CGRect(x: 150 - 57 - 4, y: 0, width: 57, height: 57))
  })
case "splash":
  let size = Int(sizeArg)!
  let w = size, h = size * 400 / 640, k = CGFloat(size) / 640
  render(w, h) { ctx in
    ctx.setFillColor(bgColor()); ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
    let ch = CGFloat(h) * 0.98, cw = ch * CGFloat(W) / CGFloat(H)
    drawFaded(ctx, CGRect(x: CGFloat(w) - cw - 4 * k, y: (CGFloat(h) - ch) / 2, width: cw, height: ch))
    let ns = NSGraphicsContext(cgContext: ctx, flipped: false)
    NSGraphicsContext.current = ns
    func text(_ t: String, _ y: CGFloat, _ sz: CGFloat, _ weight: NSFont.Weight, _ c: NSColor) {
      NSAttributedString(string: t, attributes: [.font: NSFont.systemFont(ofSize: sz * k, weight: weight), .foregroundColor: c])
        .draw(at: NSPoint(x: 48 * k, y: y * k))
    }
    text("Phantom", 176, 46, .bold, NSColor(white: 0.07, alpha: 1))
    text("Built on the open-source IntelliJ Platform", 146, 16, .regular, NSColor(white: 0.30, alpha: 1))
    text("Built from JetBrains/intellij-community (Apache 2.0)", 32, 11, .regular, NSColor(white: 0.45, alpha: 1))
  }
default: fatalError("mode")
}
