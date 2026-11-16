/* The D43 loader stub (dolstub.S): linked at the physical address BS1 copies the IPL to; BS1 jumps
   to it at 0x81300000, which maps there, and the stub's first instructions don't depend on where
   they run */
SECTIONS
{
    . = 0x01300000;
    .text : { *(.text) }
    /DISCARD/ : { *(*) }
}
