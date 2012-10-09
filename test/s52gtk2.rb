# s52gtk2.rb: GTK from Ruby - s52gtk2.c translated to ruby
#
# SD 15JAN2009 -created


#    This file is part of the OpENCview project, a viewer of ENC.
#    Copyright (C) 2000-2011  Sylvain Duclos sduclos@users.sourceforgue.net
#
#    OpENCview is free software: you can redistribute it and/or modify
#    it under the terms of the Lesser GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    OpENCview is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    Lesser GNU General Public License for more details.
#
#    You should have received a copy of the Lesser GNU General Public License
#    along with OpENCview.  If not, see <http://www.gnu.org/licenses/>.



require 'gtkglext'

require '../S52' if RUBY_PLATFORM.include?('linux')
require 'S52'    if RUBY_PLATFORM.include?('mswin')

#Gtk::init
Gtk::GL.init

w   = Gdk::screen_width
h   = Gdk::screen_height
wmm = Gdk::screen_width_mm
hmm = Gdk::screen_height_mm
S52.init(w, h, wmm, hmm)

p S52.version

#$FontName = [
#    #"arial 10",
#    #"arial 14",
#    #"arial 20"
#    "arial  8",
#    "arial 10",
#    "arial 14"
#]

class View
    attr_accessor :cLat, :cLon, :rNM, :north
end
$_view = View.new()

class Extent
    attr_accessor :s, :w, :n, :e
end
#$_ext = Extent.new()

$_dx   = 0.0
$_dy   = 0.0

# scroll to a tenth of the range
$SCROLL_FAC = 0.1
$ZOOM_FAC   = 2.0
$ZOOM_INI   = 1.0       # zoom level at reset (change this)

$_c = nil;


def _usage()
    puts
    puts "Usage: s52gtk [-h] [-f] S57.."
    puts "\t-h\t:this help"
    puts "\t-f\t:S57 file to load --else search .cfg"
    puts
    puts "Mouse:"
    puts "\tRight\t:recenter"
    puts "\tLeft \t:cursor pick"
    puts
    puts "Key:"
    puts "\th    \t:this help"
    puts "\tLeft \t:move view to left"
    puts "\tRight\t:move view to right"
    puts "\tUp   \t:move view up"
    puts "\tDown \t:move view down"
    puts "\t+,=  \t:zoom in"
    puts "\t-    \t:zoom out"
    puts "\tr    \t:render"
    puts "\tESC  \t:reset view"
    puts "\tx    \t:dump all Mariner Parameter"
    puts "\tv    \t:version"
    puts "\tq    \t:quit"

    puts "S52_MAR toggle [ON/OFF] ------------"
    puts "\tt    \t:S52_MAR_SHOW_TEXT"
    puts "\tw    \t:S52_MAR_TWO_SHADES"
    puts "\ts    \t:S52_MAR_SHALLOW_PATTERN"
    puts "\to    \t:S52_MAR_SHIPS_OUTLINE"
    puts "\tl    \t:S52_MAR_FULL_SECTORS"
    puts "\tb    \t:S52_MAR_SYMBOLIZED_BND"
    puts "\tp    \t:S52_MAR_SYMPLIFIED_PNT"
    puts "\tu    \t:S52_MAR_SCAMIN"

    puts "S52_MAR meter [+-] ------------"
    puts "\tcC   \t:S52_MAR_SAFETY_CONTOUR"
    puts "\tdD   \t:S52_MAR_SAFETY_DEPTH"
    puts "\taA   \t:S52_MAR_SHALLOW_CONTOUR"
    puts "\teE   \t:S52_MAR_DEEP_CONTOUR"
    puts "\tfF   \t:S52_MAR_DISTANCE_TAGS"
    puts "\tgG   \t:S52_MAR_TIME_TAGS"

    puts "S52_MAR_DISP_CATEGORY"
    puts "\t7    \t:DISPLAYBASE ('D' - 68)"
    puts "\t8    \t:STANDARD    ('S' - 83)"
    puts "\t9    \t:OTHER       ('O' - 79)"

    puts "S52_MAR_COLOR_PALETTE"
    puts "\t1    \t:DAY_BRIGHT"
    puts "\t2    \t:DAY_BLACKBACK"
    puts "\t3    \t:DAY_WHITEBACK"
    puts "\t4    \t:DUSK"
    puts "\t5    \t:NIGHT"
    puts
    puts "SOUNDING ----------------"
    puts "\tn    \t:S52_MAR_RASTER_SOUNDG [ON/OFF]"
    puts "\tmM   \t:S52_MAR_DATUM_OFFSET [+-] (raster_soundg must be ON)"
    puts "\ti    \t:S52_MAR_ANTIALIAS [ON/OFF]"
    puts "\tj    \t:S52_MAR_QUAPNT01 [ON/OFF]"

end

#############  Drawing Window  #############
drawing_area = Gtk::DrawingArea.new
drawing_area.set_size_request(800, 600)


glconfig = Gdk::GLConfig.new(Gdk::GLConfig::MODE_RGBA    |
                             Gdk::GLConfig::MODE_DEPTH   |
                             Gdk::GLConfig::MODE_STENCIL |
                             Gdk::GLConfig::MODE_DOUBLE
                             )

#glconfig = Gdk::GLConfig.new(Gdk::GLConfig::MODE_RGBA    |
#                             Gdk::GLConfig::MODE_DEPTH   |
#                             Gdk::GLConfig::MODE_STENCIL
#                             )

exit 1 if glconfig.nil?

drawing_area.set_gl_capability(glconfig, nil, true, Gdk::GL::RGBA_TYPE)

#p glconfig.has_stencil_buffer?

def _computeView(view)
# reset global var
    if RUBY_PLATFORM.include?('x86_64')
      #ext = S52::S52_extent.new()
      s = FFI::MemoryPointer.new(:double)
      w = FFI::MemoryPointer.new(:double)
      n = FFI::MemoryPointer.new(:double)
      e = FFI::MemoryPointer.new(:double)
    else
      ext = S52::S52_extent.malloc
    end

    #r   = S52.getCellExtent(nil, ext)
    r   = S52.getCellExtent(nil, s,w,n,e)
    #r   = S52.getCellExtent($_c, ext)
    #p s,w,n,e


    return if r == 0

    #p ext[:s], ext[:w], ext[:n], ext[:e]

    if RUBY_PLATFORM.include?('x86_64')
      #view[:cLat] =  (ext[:n] + ext[:s]) / 2.0           #/
      #view[:cLon] =  (ext[:e] + ext[:w]) / 2.0           #/
      ##view[:rNM]  = $ZOOM_INI
      #view[:rNM]  = ((ext[:n] - ext[:s]) / 2.0) * 60.0   #/

      #view.cLat =  (ext[:n] + ext[:s]) / 2.0           #/
      #view.cLon =  (ext[:e] + ext[:w]) / 2.0           #/
      #view.rNM  = ((ext[:n] - ext[:s]) / 2.0) * 60.0   #/

      view.cLat  =  (n.get_double(0) + s.get_double(0)) / 2.0           #/
      view.cLon  =  (e.get_double(0) + w.get_double(0)) / 2.0           #/
      view.rNM   = ((n.get_double(0) - s.get_double(0)) / 2.0) * 60.0   #/
      view.north =  0.0
    else
      view.cLat =  (ext.n + ext.s) / 2.0                 #/
      view.cLon =  (ext.e + ext.w) / 2.0                 #/
      #view.rNM  = $ZOOM_INI
      view.rNM  = ((ext.n - ext.s) / 2.0) * 60.0         #/
    end
end

def _resetView(view)
    _computeView(view)
    S52.setView(view.cLat, view.cLon, view.rNM, view.north)
end


#drawing_area.signal_connect_after("realize") do |w|
#  glcontext  = w.gl_context
#  gldrawable = w.gl_drawable
#
#  gldrawable.gl_begin(glcontext) do
#        $FontName.each do |fname|
#            fontDL        = GL.GenLists(256)
#            pangoFontDesc = Pango::FontDescription.new(fname)
#            font          = Gdk::GL.use_pango_font(pangoFontDesc, 0, 256, fontDL)
#            S52.setFont(fontDL) if font
#        end
#    true
#  end
#end

drawing_area.signal_connect("configure_event") do |w, e|
  glcontext  = w.gl_context
  gldrawable = w.gl_drawable

  gldrawable.gl_begin(glcontext) do
    view = $_view

    _computeView(view)
    p view.cLat, view.cLon, view.rNM, view.north
    S52.setView(view.cLat, view.cLon, view.rNM, view.north)

    S52.setViewPort(0, 0, w.allocation.width, w.allocation.height)

    true
  end
end

drawing_area.signal_connect("expose_event") do |w,e|
  glcontext  = w.gl_context
  gldrawable = w.gl_drawable

  gldrawable.gl_begin(glcontext) do

     S52::draw

     if gldrawable.double_buffered?
        gldrawable.swap_buffers
     end

  end
  true
end

drawing_area.unset_flags(Gtk::Window::DOUBLE_BUFFERED)
drawing_area.show


#############  Main Window  #############

window = Gtk::Window.new

# need to do this at the beggining !?
window.set_events(Gdk::Event::BUTTON_PRESS_MASK|Gdk::Event::BUTTON_RELEASE_MASK)
window.signal_connect("button_release_event") do |w,e|
    case e.button
        #when 3 then S52.centerAt(e.x, e.y); w.queue_draw
        when 1 then obj = S52.pickAt(e.x, e.y); puts "OBJ(#{e.x}, #{e.y}): #{obj}"
        #when 1 then S52.pickAt(e.x, e.y); #puts "OBJ(#{e.x}, #{e.y}): #{obj}"
        else p e
    end

    true
end

window.title = "s52gtk2.rb"
window.add(drawing_area)
window.unset_flags(Gtk::Window::DOUBLE_BUFFERED)

# cursor
crs = Gdk::Cursor.new(Gdk::Cursor::CROSS)
#crs = Gdk::Cursor.new(Gdk::Cursor::CROSS_REVERSE)
#crs = Gdk::Cursor.new(Gdk::Cursor::CROSSHAIR)

#window.window.cursor=crs


def _scroll(w, e)
    case e.keyval
     when Gdk::Keyval::GDK_Left  then $_view.cLon -= $_view.rNM/(60.0*10.0); S52.setView($_view.cLat, $_view.cLon, $_view.rNM, $_view.north)
     when Gdk::Keyval::GDK_Right then $_view.cLon += $_view.rNM/(60.0*10.0); S52.setView($_view.cLat, $_view.cLon, $_view.rNM, $_view.north)
     when Gdk::Keyval::GDK_Up    then $_view.cLat += $_view.rNM/(60.0*10.0); S52.setView($_view.cLat, $_view.cLon, $_view.rNM, $_view.north)
     when Gdk::Keyval::GDK_Down  then $_view.cLat -= $_view.rNM/(60.0*10.0); S52.setView($_view.cLat, $_view.cLon, $_view.rNM, $_view.north)
    end
end

def _zoom(w, e)
    case e.keyval
        # zoom in
    	when Gdk::Keyval::GDK_Page_Up   then $_view.rNM /= 2.0; S52.setView($_view.cLat, $_view.cLon, $_view.rNM, $_view.north)
        # zoom out
        when Gdk::Keyval::GDK_Page_Down then $_view.rNM *= 2.0; S52.setView($_view.cLat, $_view.cLon, $_view.rNM, $_view.north)
    end
end

def _toggle(paramName)

    val = S52.getMarinerParam(paramName)
    if val == 0.0
      val = S52.setMarinerParam(paramName, 1.0)
    else
      val = S52.setMarinerParam(paramName, 0.0)
    end
end

def _meterInc(paramName)
    val = 0.0

    val = S52.getMarinerParam(paramName) + 1.0
    val = S52.setMarinerParam(paramName, val)

    #return TRUE
    true
end

def _meterDec(paramName)
    val = 0.0

    val = S52.getMarinerParam(paramName) - 1.0
    val = S52.setMarinerParam(paramName, val)

    #return TRUE
    true
end

def _disp(paramName, disp)
    val = disp

    #val = S52.getMarinerParam(paramName)
    val = S52.setMarinerParam(paramName, val)

    #return TRUE
    true
end

def _cpal(paramName, val)
    val = S52.setMarinerParam(paramName, val)

    #return TRUE
    true
end

def _dumpParam()
    ret = S52.getMarinerParam(S52::MAR_SHOW_TEXT)
    printf('S52_MAR_SHOW_TEXT         t %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_TWO_SHADES)
    printf('S52_MAR_TWO_SHADES        w %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SAFETY_CONTOUR)
    printf('S52_MAR_SAFETY_CONTOUR    c %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SAFETY_DEPTH)
    printf('S52_MAR_SAFETY_DEPTH      d %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SHALLOW_CONTOUR)
    printf('S52_MAR_SHALLOW_CONTOUR   a %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_DEEP_CONTOUR)
    printf('S52_MAR_DEEP_CONTOUR      e %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SHALLOW_PATTERN)
    printf('S52_MAR_SHALLOW_PATTERN   s %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SHIPS_OUTLINE)
    printf('S52_MAR_SHIPS_OUTLINE     o %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_DISTANCE_TAGS)
    printf('S52_MAR_DISTANCE_TAGS     f %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_TIME_TAGS)
    printf('S52_MAR_TIME_TAGS         g %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_FULL_SECTORS)
    printf('S52_MAR_FULL_SECTORS      l %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SYMBOLIZED_BND)
    printf('S52_MAR_SYMBOLIZED_BND    b %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SYMPLIFIED_PNT)
    printf('S52_MAR_SYMPLIFIED_PNT    p %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_DISP_CATEGORY)
    printf('S52_MAR_DISP_CATEGORY   7-9 %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_COLOR_PALETTE)
    printf('S52_MAR_COLOR_PALETTE   1-5 %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_FONT_SOUNDG)
    printf('S52_MAR_RASTER_SOUNDG     n %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_DATUM_OFFSET)
    printf('S52_MAR_DATUM_OFFSET      m %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_SCAMIN)
    printf('S52_MAR_SCAMIN            u %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_ANTIALIAS)
    printf('S52_MAR_ANTIALIAS         i %4.1f', ret); puts
    ret = S52.getMarinerParam(S52::MAR_QUAPNT01)
    printf('S52_MAR_QUAPNT01          i %4.1f', ret); puts

    true
end

window.signal_connect_after("key_release_event") do |w, e|
    case e.keyval
      when Gdk::Keyval::GDK_Left,
           Gdk::Keyval::GDK_Right,
           Gdk::Keyval::GDK_Up,
           Gdk::Keyval::GDK_Down  then _scroll(w, e)

      when Gdk::Keyval::GDK_Page_Up,
           Gdk::Keyval::GDK_Page_Down then _zoom(w, e)

      when Gdk::Keyval::GDK_Escape then _resetView($_view)
      when Gdk::Keyval::GDK_h     then _usage()
      when Gdk::Keyval::GDK_r     then w.queue_draw
      when Gdk::Keyval::GDK_v     then S52.version()
      when Gdk::Keyval::GDK_x     then _dumpParam()
      when Gdk::Keyval::GDK_q     then Gtk.main_quit

      when Gdk::Keyval::GDK_t     then _toggle(S52::MAR_SHOW_TEXT)
      when Gdk::Keyval::GDK_w     then _toggle(S52::MAR_TWO_SHADES)
      when Gdk::Keyval::GDK_s     then _toggle(S52::MAR_SHALLOW_PATTERN)
      when Gdk::Keyval::GDK_o     then _toggle(S52::MAR_SHIPS_OUTLINE)
      when Gdk::Keyval::GDK_l     then _toggle(S52::MAR_FULL_SECTORS)
      when Gdk::Keyval::GDK_b     then _toggle(S52::MAR_SYMBOLIZED_BND)
      when Gdk::Keyval::GDK_p     then _toggle(S52::MAR_SYMPLIFIED_PNT)
      when Gdk::Keyval::GDK_n     then _toggle(S52::MAR_RASTER_SOUNDG)
      when Gdk::Keyval::GDK_u     then _toggle(S52::MAR_SCAMIN)
      when Gdk::Keyval::GDK_i     then _toggle(S52::MAR_ANTIALIAS)
      when Gdk::Keyval::GDK_j     then _toggle(S52::MAR_QUAPNT01)

      when Gdk::Keyval::GDK_c     then _meterInc(S52::MAR_SAFETY_CONTOUR)
      when Gdk::Keyval::GDK_C     then _meterDec(S52::MAR_SAFETY_CONTOUR)
      when Gdk::Keyval::GDK_d     then _meterInc(S52::MAR_SAFETY_DEPTH)
      when Gdk::Keyval::GDK_D     then _meterDec(S52::MAR_SAFETY_DEPTH)
      when Gdk::Keyval::GDK_a     then _meterInc(S52::MAR_SHALLOW_CONTOUR)
      when Gdk::Keyval::GDK_A     then _meterDec(S52::MAR_SHALLOW_CONTOUR)
      when Gdk::Keyval::GDK_e     then _meterInc(S52::MAR_DEEP_CONTOUR)
      when Gdk::Keyval::GDK_E     then _meterDec(S52::MAR_DEEP_CONTOUR)
      when Gdk::Keyval::GDK_f     then _meterInc(S52::MAR_DISTANCE_TAGS)
      when Gdk::Keyval::GDK_F     then _meterDec(S52::MAR_DISTANCE_TAGS)
      when Gdk::Keyval::GDK_g     then _meterInc(S52::MAR_TIME_TAGS)
      when Gdk::Keyval::GDK_G     then _meterDec(S52::MAR_TIME_TAGS)
      when Gdk::Keyval::GDK_m     then _meterInc(S52::MAR_DATUM_OFFSET)
      when Gdk::Keyval::GDK_M     then _meterDec(S52::MAR_DATUM_OFFSET)

      when Gdk::Keyval::GDK_7     then _disp(S52::MAR_DISP_CATEGORY, 68.0)  # DISPLAYBASE
      when Gdk::Keyval::GDK_8     then _disp(S52::MAR_DISP_CATEGORY, 83.0)  # STANDARD
      when Gdk::Keyval::GDK_9     then _disp(S52::MAR_DISP_CATEGORY, 79.0)  # OTHER
      when Gdk::Keyval::GDK_0     then _disp(S52::MAR_DISP_CATEGORY, 65.0)  # ALL (debug)

      when Gdk::Keyval::GDK_1     then _cpal(S52::MAR_COLOR_PALETTE,  0.0)  # DAY_BRIGHT
      when Gdk::Keyval::GDK_2     then _cpal(S52::MAR_COLOR_PALETTE,  1.0)  # DAY_BLACKBACK
      when Gdk::Keyval::GDK_3     then _cpal(S52::MAR_COLOR_PALETTE,  2.0)  # DAY_WHITEBACK
      when Gdk::Keyval::GDK_4     then _cpal(S52::MAR_COLOR_PALETTE,  3.0)  # DUSK
      when Gdk::Keyval::GDK_5     then _cpal(S52::MAR_COLOR_PALETTE,  4.0)  # NIGHT
      else
           #print("key: 0x%04x\n", e.keyval)
           p e.keyval

    end

    # redraw
    w.queue_draw

    true
    #false
end

window.signal_connect("delete_event") do
    Gtk.main_quit
    true
end

def _setupS52()
    S52.loadCell(nil, nil)

    # set standard display
    #_disp("        S52::MAR_DISP_CATEGORY", 83.0)
    #_toggle("S52::MAR_SHALLOW_PATTERN")

    S52.toggleObjClass("M_COVR")
    S52.toggleObjClass("M_NPUB")
    S52.toggleObjClass("M_NSYS")
    S52.toggleObjClass("M_QUAL")

    S52.setMarinerParam(S52::MAR_SHOW_TEXT,      11.0)
    S52.setMarinerParam(S52::MAR_TWO_SHADES,      0.0)
    S52.setMarinerParam(S52::MAR_SAFETY_CONTOUR, 10.0)
    S52.setMarinerParam(S52::MAR_SAFETY_DEPTH,   10.0)
    S52.setMarinerParam(S52::MAR_SHALLOW_CONTOUR, 5.0)
    S52.setMarinerParam(S52::MAR_DEEP_CONTOUR,   11.0)
    S52.setMarinerParam(S52::MAR_SHALLOW_PATTERN, 0.0)

    # not implemented yet
    S52.setMarinerParam(S52::MAR_SHIPS_OUTLINE,   1.0)
    #S52.setMarinerParam(S52::MAR_DISTANCE_TAGS,   0.0)
    #S52.setMarinerParam(S52::MAR_TIME_TAGS,       0.0)

    S52.setMarinerParam(S52::MAR_FULL_SECTORS,    1.0)
    S52.setMarinerParam(S52::MAR_SYMBOLIZED_BND,  1.0)
    S52.setMarinerParam(S52::MAR_SYMPLIFIED_PNT,  1.0)
    S52.setMarinerParam(S52::MAR_DISP_CATEGORY,  79.0)
    S52.setMarinerParam(S52::MAR_COLOR_PALETTE,   0.0)
    S52.setMarinerParam(S52::MAR_FONT_SOUNDG,     1.0)
    S52.setMarinerParam(S52::MAR_DATUM_OFFSET,    0.0)
    S52.setMarinerParam(S52::MAR_SCAMIN,          1.0)
    S52.setMarinerParam(S52::MAR_QUAPNT01,        0.0)

    S52.setMarinerParam(S52::MAR_ANTIALIAS,       1.0)
end

_setupS52()

window.show_all

Gtk.main

S52.done

