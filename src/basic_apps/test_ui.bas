UI_CLS()
UI_RECT(8, 8, 304, 410, 0x1E1E1E)
UI_LABEL("MY-BASIC + LVGL", 16, 20)

status = NET_HTTP_GET("http://example.com")
UI_LABEL(status, 16, 55)
FS_WRITE("/apps/last_http.txt", status)

UI_LABEL("Touchez l'ecran pour lire X/Y", 16, 130)
x = TOUCH_X()
y = TOUCH_Y()
UI_LABEL_I(x, 16, 155)
UI_LABEL_I(y, 100, 155)

UI_LABEL("Fichiers /apps:", 16, 200)
UI_LABEL(FS_LIST("/apps"), 16, 225)
END
