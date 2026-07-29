kanka dur. Gene cpu render ediyor.

Bak olay şu oalcak. reed opengl gibi olacka bir bir. Rreed contenxt açılınca 0 a yeni bir frame bbuffer üretecek. Sonra üsütne opengl tarzında ama benim stilimle render edecek. Sonra end scene olacka dx11 deki gibi app ise app in contextini windwo manager alıp render edecek. windwomanager ise direkt oalrak gerekli şekilde render  ip çizime gönderecek. rasterizn compositionu direkt oalrak gpu ya bırakacaksın kanka. hiç bir şeklde aptal aptal çzim yapmayacaksın. cpu 1 çizgi ble çizemez.

reed rendere ttiğpi frame bufferları backbuffera yazacak. reed == opengl kanka. killim == imguiDrawlist.

bu şekilde anla. killim ctx.renderDrawRect çşeklinde rect çizecek bunu reeda verecek reed da o anki framebuffera yazzacak amk.

en sonra da gpu ya gönderecek BU KADAR!

amaşuanda çok karışık ypaılmış cpu var farklı şeyelr var herpsi karışmış karman çorman oılmuş bu yünde fixle lütfen.

Yapılamaz;
- cpu rendering
- cpu resterizing
- cpu compositing

yapılmalı:
- compositing direkt reed içinde yapılmalı reed framebuffera render eder sonra üzerine diğer frame bufferin verisni ekler. Opengl deki gibi.