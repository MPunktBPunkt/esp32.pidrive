.section .rodata.demo_fat,"a"
    .global DEMO_FAT_IMAGE
    .align 4
DEMO_FAT_IMAGE:
    .incbin "src/msc/demo_fat.bin"
    .size DEMO_FAT_IMAGE, .-DEMO_FAT_IMAGE
