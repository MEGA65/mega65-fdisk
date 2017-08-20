
	.export _charset

_charset:	.word charset_data
	
charset_data:
	.incbin     "ascii8x8.bin"
