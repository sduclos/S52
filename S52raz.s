# S52raz.s: make an ELF object from PLib data
#
#
#    This file is part of the OpENCview project, a viewer of ENC.
#    Copyright (C) 2000-2015 Sylvain Duclos sduclos@users.sourceforge.net
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


        #.align  32   # to large for ARM build 
        .align  8

         # definition of symbol name
        .global S52raz
        .global S52razLen
        .data

S52raz: # fill data segment
        .incbin "S52raz-3.2.rle"
        
S52razLen: 
        .long   . - S52raz




# this work also:
# $ objcopy -v -I binary -O elf32-little \
#--rename-section .data=.rodata in_file out_file
#
# where symbol are:
#   _binary_S52raz_3_2_rle_start - is the start
#   _binary_S52raz_3_2_rle_end   - is the end

