// Ukrainian translation of ../en/settings.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
// The settings.detail.* block translates the firmware's own error sentences (firmwareText.ts);
// it keeps the device's lowercase sentence starts where the English has them.
import type { Message } from "../../format.ts";
import type { settings as source } from "../en/settings.ts";

export const settings: Record<keyof typeof source, Message> = {
  "settings.title": "Налаштування",
  "settings.readOnly.headline": "Ці налаштування записані новішою прошивкою",
  "settings.readOnly.detail":
    "Ця збірка не розуміє формат збереженої конфігурації, тому відмовляється "
    + "перезаписувати її, щоб не пошкодити. Перепрошийте пристрій попередньою прошивкою "
    + "або скиньте налаштування кнопкою, перш ніж щось тут змінювати.",
  "settings.load.hint":
    "Налаштування брокера та пристрою неможливо показати, доки їх не вдасться прочитати. "
    + "Форма Wi-Fi вище від них не залежить.",
  "settings.tryAgain": "Спробувати ще раз",
  "settings.loading": "Зчитування конфігурації пристрою…",
  "settings.save": "Зберегти налаштування",
  "settings.saved": "Налаштування збережено",
  "settings.saveBar.hint": "Зберігає налаштування брокера та пристрою разом. Мережі це не торкається.",
  "settings.notSent": "Не надіслано — нічого не залишило цю сторінку",

  "settings.broker.title": "MQTT-брокер",
  "settings.broker.subtitle": "Куди пристрій публікує дані та де їх знаходить Home Assistant.",
  "settings.broker.host.label": "Хост брокера",
  "settings.broker.host.placeholder": "192.168.1.10 або homeassistant.local",
  "settings.broker.port.label": "Порт брокера",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Ім'я користувача брокера",
  "settings.broker.password.label": "Пароль брокера",
  "settings.broker.password.warning":
    "У цій версії це з'єднання не шифрується, тому цей пароль видно всьому, що є у вашій "
    + "мережі. Не використовуйте пароль, яким ви користуєтесь деінде.",
  "settings.broker.topicPrefix.label": "Префікс тем",
  "settings.broker.discovery.label": "Публікувати виявлення Home Assistant",

  "settings.device.title": "Пристрій",
  "settings.device.subtitle": "Як він називається, як поводиться і хто може його змінювати.",
  "settings.device.name.label": "Назва пристрою",
  "settings.device.name.placeholder": "Вентиляція",
  "settings.device.name.hint":
    "Лише назва для показу. Ідентичність пристрою — його client id брокера та device id "
    + "у Home Assistant — походить від його MAC-адреси, тож перейменування тут не може "
    + "створити другий пристрій у Home Assistant.",
  "settings.device.password.label": "Пароль вебінтерфейсу",
  "settings.device.password.warnTitle": "Для цього пристрою немає «забув пароль».",
  "settings.device.password.warn2a": "Утримання кнопки ",
  "settings.device.password.warn2em": "під час",
  "settings.device.password.warn2b":
    " увімкнення живлення робить дещо зовсім інше — переводить чіп у завантажувач "
    + "прошивки, що виглядає точнісінько як мертвий пристрій. Спершу дайте йому увімкнутися, "
    + "зачекайте, і лише тоді утримуйте.",
  "settings.device.password.noneSet":
    "Пароль не встановлено. Будь-хто, хто може дістатися цього пристрою в мережі, може "
    + "змінити ці налаштування — прийнятний вибір для пристрою в домашній мережі, і це "
    + "свідоме значення за замовчуванням.",

  "settings.wifi.title": "Wi-Fi",
  "settings.wifi.subtitle": "Список показує, що чує цей пристрій, а не те, що показує ваш телефон.",
  "settings.wifi.network.label": "Мережа",
  "settings.wifi.network.choose": "— оберіть мережу —",
  "settings.wifi.network.currentlyConfigured": "налаштована зараз",
  "settings.wifi.network.open": "відкрита",
  "settings.wifi.network.manualOption": "Інша / прихована мережа — введіть назву…",
  "settings.wifi.network.ssidLabel": "Назва мережі (SSID)",
  "settings.wifi.network.ssidPlaceholder": "точно так, як транслює маршрутизатор",
  "settings.wifi.scan.again": "Сканувати ще раз",
  "settings.wifi.scan.start": "Пошук мереж",
  "settings.wifi.chooseFromList": "Обрати зі списку",
  "settings.wifi.typeInstead": "Ввести назву замість цього",
  "settings.wifi.scan.hint":
    "Назва, яка є на вашому телефоні, але відсутня в цьому списку, — звичайна причина, "
    + "чому пристрій \"не знаходить\" мережу, яка явно існує: більшість маршрутизаторів "
    + "транслюють одну назву в обох діапазонах, а цей пристрій не має радіомодуля 5 ГГц. "
    + "Введіть її вручну, якщо так сталося — приховані мережі ніколи не з'являються в "
    + "жодному скануванні, їх можна лише ввести.",
  "settings.wifi.password.label": "Пароль Wi-Fi",
  "settings.wifi.password.fallbackNetworkName": "збережена мережа",
  "settings.wifi.notice.newNetworkFallback": "нова мережа",
  "settings.wifi.password.hintUnencrypted":
    "Якщо ви перебуваєте у власній мережі налаштування пристрою, ця мережа відкрита: цей "
    + "ключ один раз перетне її незашифрованим. Ніщо на сторінці браузера, що подається "
    + "через звичайний HTTP, не може цьому запобігти.",
  "settings.wifi.network.openHint": "{ssid} — відкрита, залиште поле пароля порожнім.",
  "settings.wifi.network.securedWarning":
    "{ssid} захищена, а поле пароля порожнє. Пристрій спробує підключитися й отримає "
    + "відмову.",
  "settings.wifi.beforeYouPress": "Перш ніж натиснути це",
  "settings.wifi.save": "Зберегти Wi-Fi і перепідключитися",
  "settings.wifi.accepted": "Прийнято — пристрій зараз пробує",

  "settings.wifi.signal.notSeen": "не виявлено в цьому скануванні",
  "settings.wifi.signal.strong": "сильний",
  "settings.wifi.signal.good": "добрий",
  "settings.wifi.signal.weak": "слабкий",
  "settings.wifi.signal.veryWeak": "дуже слабкий",

  "settings.wifi.key.stored":
    "Ключ, уже збережений для {ssid}, буде використано знову. Він ніколи не надсилається "
    + "назад на цю сторінку, тому поле порожнє; введіть, щоб замінити його.",
  "settings.wifi.key.empty":
    "Порожнє: пристрій спробує підключитися без ключа. Правильно для відкритої мережі, "
    + "і ні для чого іншого.",
  "settings.wifi.key.typed": "Цей ключ буде надіслано пристрою так, як він введений.",

  "settings.wifi.notice.line1":
    "Пристрій відповідає на цю форму, перш ніж почати підключення, тож підтвердження тут "
    + "означає, що облікові дані прийнято — а не що {target} їх застосувала.",
  "settings.wifi.notice.line2":
    "Поки триває спроба, мережа налаштування, в якій ви зараз перебуваєте, і {target} "
    + "спільно використовують одне радіо. Ця сторінка, найімовірніше, перестане відповідати "
    + "протягом кількох секунд — це тут нормальний результат, а не збій.",
  "settings.wifi.notice.line3":
    "Після цього пристрій перебуватиме в {target}, з тією адресою, яку видасть ваш "
    + "маршрутизатор. Список підключених клієнтів вашого маршрутизатора — місце, де його "
    + "шукати.",
  "settings.wifi.notice.fallback":
    "Якщо ключ невірний, пристрій сам відновить {previous} і повернеться туди — це "
    + "остання мережа, яка справді дала цьому пристрою адресу, що й робить її єдиною "
    + "вартою повернення. Нічого скидати не потрібно.",
  "settings.wifi.notice.noFallback":
    "Цей пристрій ніколи не досягав жодної мережі, тому йому немає куди повертатися. "
    + "Якщо ключ невірний, він продовжує спроби, а власна мережа налаштування лишається в "
    + "ефірі приблизно 15 хвилин від моменту появи. Після цього мережа налаштування зникає "
    + "до перезапуску пристрою, який повертає її на кілька хвилин.",

  "settings.secret.stored.text":
    "Збережено на пристрої. Ніколи не надсилається назад, тому поле порожнє; введіть, "
    + "щоб замінити.",
  "settings.secret.stored.placeholder": "без змін — введіть, щоб замінити",
  "settings.secret.willClear.text": "Очищено. Збереження видалить збережене значення.",
  "settings.secret.willClear.placeholder": "порожньо — збереження видалить збережене значення",
  "settings.secret.willReplace.text": "Збереження замінить збережене значення.",
  "settings.secret.notSet.text": "Не встановлено.",
  "settings.secret.notSet.placeholder": "не встановлено",
  "settings.secret.willSet.text": "Збереження встановить це значення вперше.",
  "settings.error.portRange": "Порт брокера має бути цілим числом від 1 до 65535.",

  "settings.error.notImplemented.headline": "У пристрою є цей маршрут, але коду за ним ще немає",
  "settings.error.noHandler.headline": "Ця збірка прошивки не обслуговує цю кінцеву точку",
  "settings.error.passwordNeeded.headline": "Пристрій вимагає пароль вебінтерфейсу",
  "settings.error.writeRefused.headline": "Пристрій відхилив цей запис",
  "settings.error.badRequest.headline": "Пристрій не захотів це прийняти",
  "settings.error.fault.headline": "Пристрій повідомив про несправність",
  "settings.error.browserGaveNoReason": "Браузер не назвав причини.",
  "settings.error.disconnectExpected.headline": "Немає відповіді — і це тут якраз успіх",
  "settings.error.disconnectExpected.detail":
    "Пристрій відповідає до того, як перелаштує своє радіо, тож запит, що обривається "
    + "на півдорозі, зазвичай означає, що він уже намагається. ({browserSaid})",
  "settings.error.offline.headline": "Немає відповіді від пристрою",
  "settings.error.offline.detail": "Ніщо не досягло пристрою, тож він нічого про це не сказав. ({browserSaid})",

  "settings.detail.ssid": "назва мережі має містити від 1 до 32 символів",
  "settings.detail.psk":
    "пароль мережі має містити від 8 до 63 символів, або рівно 64 шістнадцяткові цифри, "
    + "або бути порожнім для відкритої мережі",
  "settings.detail.wifiPair":
    "назва мережі та її пароль зберігаються як один запис: надсилайте обидва й вводьте "
    + "пароль знову щоразу, коли змінюється назва",
  "settings.detail.host":
    "адреса брокера має бути іменем хоста або IP-адресою, без схеми та без порту",
  "settings.detail.port": "порт брокера має бути від 1 до 65535",
  "settings.detail.user": "ім'я користувача брокера довше, ніж може зберегти цей пристрій",
  "settings.detail.brokerPassword": "пароль брокера довший, ніж може зберегти цей пристрій",
  "settings.detail.prefix":
    "префікс тем має містити від 1 до 64 символів, без пробілів, + чи #, і без порожнього "
    + "рівня",
  "settings.detail.name":
    "назва пристрою має містити від 1 до 32 символів і не містити керівних символів",
  "settings.detail.uiPassword":
    "вебпароль має містити від 8 до 128 символів і не містити керівних символів",
  "settings.detail.noHash": "цей пристрій не зміг захистити пароль саме зараз; нічого не змінено",
  "settings.detail.readOnly":
    "ці налаштування записані новішою прошивкою, і їх не можна змінити, доки пристрій не "
    + "буде скинуто",
  "settings.detail.noDocument": "запит не містив налаштувань, тому нічого не збережено",
  "settings.detail.refused": "налаштування відхилено",
  "settings.detail.badBody": "Тіло запиту не є JSON-об'єктом, який цей пристрій може прочитати.",
  "settings.detail.badField":
    "Поле відсутнє, має неправильний тип або довше, ніж може зберегти цей пристрій.",
  "settings.detail.wrongRoute": "Мережа налаштовується через POST /api/provision, а не тут.",
  "settings.detail.notApplied": "Запит не вдалося застосувати.",
  "settings.detail.unauthorized": "не авторизовано",
  "settings.detail.forbidden": "заборонено",
  "settings.detail.notImplemented": "ще не реалізовано",
  "settings.detail.noEndpoint": "такої кінцевої точки немає",
  "settings.detail.bodyTooLarge": "тіло запиту завелике",
  "settings.detail.truncatedBody": "тіло запиту неповне",
  "settings.detail.scanUnavailable": "сканування недоступне",
  "settings.detail.scanTooLarge": "результат сканування не вміщується у відповідь",
  "settings.detail.busy": "зайнято",
  "settings.detail.statusTooLarge": "стан не вміщується у відповідь",
  "settings.detail.configTooLarge": "конфігурація не вміщується у відповідь",
  "settings.detail.emptyBody": "Пристрій відповів {1} з порожньою відповіддю.",
  "settings.detail.unreadableBody": "Пристрій відповів {1} відповіддю, яку ця сторінка не може прочитати.",

  // --- broker/device notes, the reset-button ladder, the Connection card (status.ts) ---
  "settings.broker.topicPrefix.hint":
    "Зміна префікса переносить усі топіки стану. Сутності в Home Assistant переходять "
    + "слідом і зберігають історію — опис для Home Assistant не містить префікса, тож його "
    + "переписують, а не замінюють, і другого набору ви не побачите. Якщо під час "
    + "збереження пристрій під'єднаний до брокера, він дорогою очищує топіки за старим "
    + "префіксом; якщо ні — вони лишаються там як retained-повідомлення, які вже ніхто не "
    + "читає.",
  "settings.broker.discovery.note":
    "Вимкнено: пристрій перестає публікувати свій опис для Home Assistant, але не "
    + "відкликає опублікованого раніше. Пристрій і його сутності лишаються там і далі "
    + "оновлюються — щоб прибрати їх, потрібно вручну видалити ці retained-повідомлення на "
    + "брокері.",
  "settings.device.permanentManual.label": "Утримувати рівень вентиляції",
  "settings.device.permanentManual.note":
    "Увімкнено: рівень, вибраний тут або в Home Assistant, зберігається. Вимкнено: "
    + "установка повертається до AUTO протягом двох годин, а її регулювання вологості й "
    + "температури лишаються активними — ручний режим їх вимикає.",
  "settings.device.button.p1":
    "Шлях назад — кнопка на пристрої, і все залежить від того, як довго ви її тримаєте. "
    + "Натисніть її {after} і стежте за світлом: воно стає білим, щойно натискання "
    + "помічено, між першою і п'ятою секундою повертається до звичайного кольору, на п'ятій "
    + "стає бурштиновим, на п'ятнадцятій — червоним і яскравішає з наближенням наступного "
    + "кроку.",
  "settings.device.button.p1em": "після того, як пристрій запустився",
  "settings.device.button.p2":
    "{short} — і нічого не стирається: пристрій на кілька хвилин вмикає власну мережу "
    + "{ap}. Корисно, коли він загубив домашній Wi-Fi, — але цей пароль діє і там. {gap} — "
    + "і не відбувається нічого. Цей проміжок навмисний: випадковий поштовх чи рукав нічого "
    + "не зітре.",
  "settings.device.button.p2short": "Відпустіть до першої секунди",
  "settings.device.button.p2gap": "Відпустіть між першою і п'ятою секундою",
  "settings.device.button.p3":
    "{soft}, поки світло бурштинове, — і пристрій зітре облікові дані Wi-Fi разом із цим "
    + "паролем, зберігши налаштування брокера, префікс топіків, назву пристрою і "
    + "тривалості. Він блимне тричі, перезапуститься й повернеться з тією самою мережею в "
    + "ефірі та цією сторінкою, відкритою для всіх.",
  "settings.device.button.p3strong": "Відпустіть після п'ятої секунди",
  "settings.device.button.p4":
    "{hard}, до червоного, — і пристрій зітре все, включно з налаштуваннями брокера: цей "
    + "крок — для пристрою, що залишає будинок. Він не прибирає за собою на брокері, тож "
    + "Home Assistant збереже пристрій і його сутності — якщо хочете їх прибрати, спершу "
    + "спрямуйте пристрій на інший брокер, поки він ще досяжний для цього. Після двадцяти "
    + "п'яти секунд пристрій вирішує, що кнопка застрягла, і нічого не робить; відпустіть і "
    + "почніть знову.",
  "settings.device.button.p4strong": "Тримайте довше за п'ятнадцять секунд",
  "settings.status.title": "З'єднання",
  "settings.status.noKnownGood":
    "Ця мережа жодного разу не видала адреси, тож якщо наступна не спрацює, повертатися "
    + "нема куди.",
  "settings.status.failure.notFound":
    "Цю мережу не знайдено. Перевірте назву і те, що це мережа 2,4 ГГц, — у цього "
    + "пристрою немає радіо 5 ГГц.",
  "settings.status.failure.wrongPassword":
    "Мережа відхилила ключ. Введіть його знову; збережений ключ ніколи не показується.",
  "settings.status.failure.refused":
    "Роутер відповів і відмовив пристрою. Можливо, його точка доступу переповнена або це "
    + "корпоративна мережа, яку ця форма не може описати.",
  "settings.status.failure.noAddress":
    "Пристрій приєднався до мережі, але так і не отримав адреси. Це DHCP роутера, а не "
    + "пароль.",
  "settings.status.failure.unknown":
    "Пристрій повідомив про збій, для якого ця сторінка не має опису: {failure} (причина "
    + "{reason}).",
  "settings.status.where.connected": "У мережі {ssid}, за адресою {ip}.",
  "settings.status.where.setupAp":
    "Роздає власну мережу налаштування. До неї може під'єднатися будь-хто поблизу, тому "
    + "запис заборонено, доки пристрій не приєднається до мережі.",
  "settings.status.where.trying": "Намагається приєднатися до {ssid}.",
  "settings.status.where.none": "Мережу не налаштовано.",
};
