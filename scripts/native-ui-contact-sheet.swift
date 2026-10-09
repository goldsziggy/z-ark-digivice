// Local private rendering utility. Image bytes never leave the filesystem.
import Foundation
import AppKit
let folder=CommandLine.arguments[1]
let text=try String(contentsOfFile:folder+"/screens.tsv",encoding:.utf8)
let rows=text.split(separator:"\n").dropFirst().map{String($0).components(separatedBy:"\t")}
let columns=4, tileWidth=436, tileHeight=472, top=116
let height=top+((rows.count+columns-1)/columns)*tileHeight+28
let bitmap=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:columns*tileWidth,pixelsHigh:height,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:columns*tileWidth*4,bitsPerPixel:32)!
let context=NSGraphicsContext(bitmapImageRep:bitmap)!
NSGraphicsContext.saveGraphicsState();NSGraphicsContext.current=context
NSColor(calibratedRed:0.03,green:0.07,blue:0.09,alpha:1).setFill();NSRect(x:0,y:0,width:bitmap.pixelsWide,height:height).fill()
func label(_ text:String,_ x:Int,_ y:Int,_ size:CGFloat,_ color:NSColor){let attrs:[NSAttributedString.Key:Any]=[.font:NSFont.systemFont(ofSize:size,weight:.medium),.foregroundColor:color];(text as NSString).draw(at:NSPoint(x:x,y:y),withAttributes:attrs)}
label("DIGIVICE / NATIVE SCREEN AUDIT",24,height-47,26,.white)
label("Actual 412 x 412 renderer + prepared local sprite / scene pack",24,height-75,16,.lightGray)
label("Synthetic states. Host JPEG decode. Physical screen / touch acceptance still pending.",24,height-98,14,.lightGray)
for (index,row) in rows.enumerated(){let col=index%columns, r=index/columns;let x=col*tileWidth+12;let y=height-top-(r+1)*tileHeight
 let url=URL(fileURLWithPath:folder+"/"+row[0]);guard let image=NSImage(contentsOf:url) else{fatalError("Missing rendered frame")}
 image.draw(in:NSRect(x:x,y:y+42,width:412,height:412),from:.zero,operation:.copy,fraction:1,respectFlipped:false,hints:[.interpolation:NSImageInterpolation.none])
 label(String(format:"%02d",index+1)+"  "+row[1],x+4,y+19,13,.white)
}
NSGraphicsContext.restoreGraphicsState()
try bitmap.representation(using:.png,properties:[:])!.write(to:URL(fileURLWithPath:folder+"/contact-sheet.png"),options:.atomic)
