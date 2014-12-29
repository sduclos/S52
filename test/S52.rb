# S52.rb: ruby S52 interface
#
# SD 30NOV2008 -created
# SD 16JAN2009 -mod: use 'dl' instead of 'ffi'
# SD 27APR2009 -add: setView(), getCellExtent() for zoom / pan
# SD 24MAY2009 -add: relative navigation (zoom, move)
# SD 31MAY2009 -add: ffi


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



module S52

  #same C enum for mariners' selection
  #enum :S52_MAR_param_t, [
  MAR_NONE                = 0   # default to 0
  MAR_SHOW_TEXT           = 1
  MAR_TWO_SHADES          = 2
  MAR_SAFETY_CONTOUR      = 3
  MAR_SAFETY_DEPTH        = 4
  MAR_SHALLOW_CONTOUR     = 5
  MAR_DEEP_CONTOUR        = 6
  MAR_SHALLOW_PATTERN     = 7
  MAR_SHIPS_OUTLINE       = 8
  MAR_DISTANCE_TAGS       = 9
  MAR_TIME_TAGS           = 10
  MAR_FULL_SECTORS        = 11
  MAR_SYMBOLIZED_BND      = 12
  MAR_SYMPLIFIED_PNT      = 13
  MAR_DISP_CATEGORY       = 14
  MAR_COLOR_PALETTE       = 15
  MAR_VECPER              = 16
  MAR_VECMRK              = 17
  MAR_VECSTB              = 18
  MAR_HEADNG_LINE         = 19
  MAR_BEAM_BRG_NM         = 20
  #---- experimental variables ----
  MAR_FONT_SOUNDG         = 21
  MAR_DATUM_OFFSET        = 22
  MAR_SCAMIN              = 23
  MAR_ANTIALIAS           = 24
  MAR_QUAPNT01            = 25
  MAR_DISP_OVERLAP        = 26
  MAR_NUM                 = 27  # 27 number of parameters
  #]

if RUBY_PLATFORM.include?('x86_64')
  require 'ffi'

  extend FFI::Library
  ffi_lib './libS52.so'

  #class S52_extent < FFI::Struct
  #  layout(:s, :double, :w, :double, :n, :double, :e, :double)
  #end
  #class S52_view < FFI::Struct
  #  layout(:cLat, :double, :cLon, :double, :rNM, :double)
  #end

  attach_function(:S52_init,     [:int, :int, :int, :int], :int)
  attach_function(:S52_version,  [], :string)

  attach_function(:S52_done,     [], :int)
  attach_function(:S52_setFont,  [:int], :int)
  attach_function(:S52_loadCell, [:pointer, :pointer], :int)
  attach_function(:S52_draw,     [], :int)

  attach_function(:S52_pickAt,   [:double, :double], :pointer)

  attach_function(:S52_setView,  [:double, :double, :double, :double], :int)

  attach_function(:S52_getCellExtent,   [:pointer, :pointer, :pointer, :pointer, :pointer], :int)

  attach_function(:S52_getMarinerParam, [:int], :double)
  attach_function(:S52_setMarinerParam, [:int, :double], :int)

  attach_function(:S52_toggleObjClass, [:string], :int)
  attach_function(:S52_setViewPort, [:int, :int, :int, :int], :int)

  def S52.init(w, h, wmm, hmm) S52.S52_init(w, h, wmm, hmm) end
  def S52.version()            S52.S52_version()            end
  def S52.done()               S52.S52_done()               end
  def S52.setFont(int)         S52.S52_setFont(int)         end
  def S52.loadCell(cell, cb)   S52.S52_loadCell(cell, cb)   end
  #def S52.draw()               S52.S52_draw()               end
  def S52::draw()              S52::S52_draw()              end

  def S52.pickAt(x,y)          S52.S52_pickAt(x,y)          end

  def S52.setView(lat, lon, r, n)         S52.S52_setView(lat, lon, r, n)         end

  def S52.getCellExtent(cell, s,w,n,e)    S52.S52_getCellExtent(cell, s,w,n,e)    end
  def S52.getMarinerParam(paramName)      S52.S52_getMarinerParam(paramName)      end
  def S52.setMarinerParam(paramName, val) S52.S52_setMarinerParam(paramName, val) end
  def S52.toggleObjClass(classname)       S52.S52_toggleObjClass(classname)       end
  def S52.setViewPort(x,y,w,h)            S52.S52_setViewPort(x,y,w,h)            end

else

  require 'dl/import'
  #require 'dl/struct'     # S52_extent, S52_view

  extend DL::Importable

  dlload 'libS52.so'  if RUBY_PLATFORM.include?('linux')
  dlload 'libS52.dll' if RUBY_PLATFORM.include?('mswin')

  #S52_extent = struct [
  #'double s',            # LL: Lower
  #'double w',            # LL: Left
  #'double n',            # UR: Upper
  #'double e'             # UR: Right
  #]
  #S52_view   = struct [
  #'double cLat',         # deg decimal
  #'double cLon',         # deg decimal
  #'double rNM'           # Nautical Mile
  #]

  extern 'int    S52_init(int, int, int, int)'
  extern 'char*  S52_version()'

  extern 'int    S52_done()'
  extern 'int    S52_setFont(int)'
  extern 'int    S52_loadCell(void *, void *)'
  extern 'int    S52_draw()'

  # WARNING: these don't work on X86_64 Ruby/DL (double)
  extern 'char*  S52_pickAt(double, double)'

  extern 'int    S52_setView(double, double, double, double)'

  #extern 'int    S52_getCellExtent(void *, S52_extent *)'
  extern 'int    S52_getCellExtent(void *, double *, double *, double *, double *)'
  extern 'double S52_getMarinerParam(int)'
  extern 'int    S52_setMarinerParam(int, double)'
  extern 'int    S52_toggleObjClass(char *)'
  extern 'int    S52_setViewPort(int, int, int, int)'

  # alias for convinence
  # WARNING: need 's52' NOT 'S52' because Ruby DL lower the case
  # since upper case mean somthing else in Rube (const)
  def S52.init(w, h, wmm, hmm) S52.s52_init(w, h, wmm, hmm) end
  def S52.version()            S52.s52_version()            end
  def S52.done()               S52.s52_done()               end
  def S52.setFont(int)         S52.s52_setFont(int)         end
  def S52.loadCell(cell, cb)   S52.s52_loadCell(cell, cb)   end
  def S52.draw()               S52.s52_draw()               end

  def S52.pickAt(x,y)          S52.s52_pickAt(x,y)          end

  def S52.setView(lat, lon, r, n)         S52.s52_setView(lat, lon, r, n)         end

  def S52.getCellExtent(cell, s,w,n,e)    S52.s52_getCellExtent(cell, s,w,n,e)    end
  def S52.getMarinerParam(paramName)      S52.s52_getMarinerParam(paramName)      end
  def S52.setMarinerParam(paramName, val) S52.s52_setMarinerParam(paramName, val) end
  def S52.toggleObjClass(classname)       S52.s52_toggleObjClass(classname)       end
  def S52.setViewPort(x,y,w,h)            S52.s52_setViewPort(x,y,w,h)            end

end   # if x86_64

end   # module S52

#p S52.version
