.section .assets,"a"
.align 4                 /* Ensures 4-byte alignment */
.global observer_assets_start
observer_assets_start:
    .incbin "assets.zip"
.global observer_assets_end
observer_assets_end:
