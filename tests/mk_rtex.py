import sys, os
d = sys.argv[1]
body = " ".join(["ff"] * 64)
x = ('﻿<?xml version="1.0" encoding="utf-8"?>\r\n<nw4r_3dif version="1.4.0">\r\n<body>\r\n<rtex>\r\n'
     '<image format="i8" palette_name=""\r\n\twidth="8" height="8"\r\n\tmipmap_level="1"\r\n\tdata_unit="u8" data_size="64"\r\n\tpath="x.bmp"\r\n>\r\n\t'
     + body + '\r\n</image>\r\n</rtex>\r\n</body>\r\n</nw4r_3dif>\r\n')
open(os.path.join(d, "t.rtex"), "w", encoding="utf-8", newline="").write(x)
