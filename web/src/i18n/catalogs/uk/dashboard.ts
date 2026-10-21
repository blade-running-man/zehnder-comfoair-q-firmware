// Ukrainian translation of ../en/dashboard.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Keep every {placeholder}
// verbatim. Draft wording; the owner reviews it.
import type { Message } from "../../format.ts";
import type { dashboard as source } from "../en/dashboard.ts";

export const dashboard: Record<keyof typeof source, Message> = {
  "dashboard.title.suffix": "· Головна",
  "dashboard.tagline": "Ваша вентиляція з рекуперацією тепла з першого погляду — що надходить, "
    + "куди йде повітря і як ним керувати.",
  "dashboard.conn.live": "Наживо",
  "dashboard.conn.waiting": "Підключено, очікування перших показань",
  "dashboard.conn.connecting": "Підключення…",
  "dashboard.conn.closed": "Від'єднано — показники нижче можуть бути застарілими",

  "dashboard.season.heating": "Опалювальний сезон",
  "dashboard.season.cooling": "Сезон охолодження",
  "dashboard.season.mild": "Міжсезоння",

  "dashboard.hero.stopped.status": "Приплив призупинено — нічого не надходить",
  "dashboard.hero.stopped.line": "Припливний вентилятор зупинено, тож свіже повітря не "
    + "надходить. Витяжка продовжує працювати.",
  "dashboard.hero.bypass.status": "Охолоджуємо дім нічним повітрям",
  "dashboard.hero.bypass.line": "Надворі {temp}, і байпас відкрито, тож це повітря пройшло повз "
    + "теплообмінник — воно охолоджує дім, а не зігріває його.",
  "dashboard.hero.warmed.status": "Свіже повітря, підігріте дорогою",
  "dashboard.hero.warmed.line": "Надворі {temp}. Теплообмінник повернув {lift} із {avail} °C "
    + "тепла, яке мав у розпорядженні.",
  "dashboard.hero.tempered.status": "Свіже повітря, м'яко вирівняне",
  "dashboard.hero.tempered.line": "Надворі {temp}. Теплообмінник наблизив температуру вхідного "
    + "повітря до температури ваших кімнат.",
  "dashboard.hero.near.status": "Свіже повітря, близьке до зовнішньої температури",
  "dashboard.hero.near.line": "Надворі {temp}. Усередині й надворі температура настільки "
    + "близька, що повертати майже нічого.",
  "dashboard.hero.waiting.status": "Очікування перших показань",
  "dashboard.hero.waiting.line": "Панель підключена й чекає на дані від установки.",
  "dashboard.hero.intoRooms": "у ваші кімнати",

  "dashboard.chip.supplyStopped": "Припливний вентилятор зупинено",
  "dashboard.chip.bypassOpen": "Байпас відкрито",
  "dashboard.chip.filtersOverdue": "Заміну фільтрів прострочено",
  "dashboard.chip.filtersDue": "Заміна фільтрів через {days} дн.",

  "dashboard.mode.label": "Режим вентиляції",
  "dashboard.mode.choose": "Оберіть, скільки повітря",
  "dashboard.mode.help.Auto": "установка сама задає витрату повітря — залиште так",
  "dashboard.mode.help.Away": "мінімум повітря, поки вдома нікого немає",
  "dashboard.mode.help.Low": "тиха фонова вентиляція",
  "dashboard.mode.help.Medium": "щоденне свіже повітря для житлового дому",
  "dashboard.mode.help.High": "більше повітря для людної чи вологої кімнати",
  "dashboard.mode.help.Boost": "максимум повітря на заданий час, потім назад до розкладу",
  "dashboard.stat.airflow": "Витрата повітря",
  "dashboard.stat.power": "Потужність",
  "dashboard.stat.recovered": "Рекуперація тепла",
  "dashboard.stat.bypassed": "Навмисно в обхід",

  "dashboard.lift.title": "Приріст тепла",
  "dashboard.lift.bypassed": "В обхід",
  "dashboard.lift.nothing": "нічого не надходить",
  "dashboard.lift.skips": "свіже повітря минає теплообмінник",
  "dashboard.lift.warmer": "тепліше, ніж надворі",
  "dashboard.lift.cooler": "холодніше, ніж надворі",
  "dashboard.lift.aria.nothing": "Приріст тепла: нічого не надходить.",
  "dashboard.lift.aria.bypassed": "Приріст тепла: в обхід — свіже повітря минає теплообмінник.",
  "dashboard.lift.aria.warmed": "Приріст тепла: вхідне повітря нагріто на {lift} °C, від "
    + "{outdoor} надворі до {extract} у ваших кімнатах.",
  "dashboard.lift.outside": "надворі",
  "dashboard.lift.outsideAt": "{temp} надворі",
  "dashboard.lift.rooms": "ваші кімнати",
  "dashboard.lift.roomsAt": "{temp} у кімнатах",

  "dashboard.bypass.title": "Куди йде повітря",
  "dashboard.bypass.key": "Байпас",
  "dashboard.bypass.bypassed": "Теплообмінник в обхід",
  "dashboard.bypass.recovered": "{pct}% тепла повернуто",
  "dashboard.bypass.exchange": "Теплообмін",
  "dashboard.bypass.supplyStopped": "Приплив зупинено",
  "dashboard.bypass.summaryOpen": "Байпас відкрито: зовнішнє повітря {outdoor} минає "
    + "теплообмінник; повітря з кімнат {extract} виходить при {exhaust}.",
  "dashboard.bypass.summaryClosed": "Протитоковий теплообмінник: зовнішнє повітря {outdoor} "
    + "нагрівається дорогою всередину до {supply}; повітря з кімнат {extract} охолоджується "
    + "дорогою назовні до {exhaust}.",
  "dashboard.bypass.outdoorAir": "Зовнішнє повітря",
  "dashboard.bypass.exhaust": "Викид",
  "dashboard.bypass.fromRooms": "З кімнат",
  "dashboard.bypass.toRooms": "У кімнати",
  "dashboard.bypass.zoneOut": "Надворі",
  "dashboard.bypass.zoneHome": "Ваш дім",
  "dashboard.bypass.fanSupply": "Приплив",
  "dashboard.bypass.fanExtract": "Витяжка",
  "dashboard.bypass.supplyFlow": "Витрата припливу",
  "dashboard.bypass.exhaustFlow": "Витрата викиду",
  "dashboard.bypass.outdoorMean": "Середня надворі",
  "dashboard.bypass.set": "Налаштувати байпас",
  "dashboard.bypass.modeAria": "Режим байпаса",

  "dashboard.filters.title": "Фільтри",
  "dashboard.filters.unknown": "Ще немає даних",
  "dashboard.filters.good": "Фільтри в нормі",
  "dashboard.filters.soon": "Замовте заміну",
  "dashboard.filters.overdue": "Замініть фільтри",
  "dashboard.filters.tagOutdoor": "Зовнішнє повітря",
  "dashboard.filters.tagExtract": "Витяжка",
  "dashboard.filters.days": "днів",
  "dashboard.filters.daysLeft": {
    one: "день залишився", few: "дні залишилось", many: "днів залишилось", other: "дня залишилось",
  },
  "dashboard.filters.daysOverdue": {
    one: "день прострочено", few: "дні прострочено", many: "днів прострочено",
    other: "дня прострочено",
  },
  "dashboard.filters.fitted": "Встановлено",
  "dashboard.filters.due": "Заміна",
  "dashboard.filters.noteOverdue": "Обидва фільтри — зовнішнього повітря й витяжки — змінюють "
    + "разом, а після нуля установка припиняє відлік.",
  "dashboard.filters.noteCycle": "Обидва фільтри — зовнішнього повітря й витяжки — змінюють "
    + "разом, за циклом установки в {cycle} днів.",

  "dashboard.comfort.title": "Комфорт і температура",
  "dashboard.comfort.seasonNote": "Задається автоматично — установка стежить за 5-денним "
    + "середнім надворі, тож сезон не обирають вручну.",
  "dashboard.comfort.seasonNoteMean": "Задається автоматично — установка стежить за 5-денним "
    + "середнім надворі ({mean}), тож сезон не обирають вручну.",
  "dashboard.comfort.profile": "Температурний профіль",
  "dashboard.comfort.aiming": "Ціль: {temp}",
  "dashboard.comfort.profileHint": "Прохолодніше чи тепліше припливне повітря. Установка — не "
    + "термостат: вона зсуває власну ціль, але не може тримати точну температуру в кімнаті.",
  "dashboard.comfort.passive.label": "Пасивне охолодження",
  "dashboard.comfort.passive.help": "Прохолодне зовнішнє повітря допомагає знизити температуру, "
    + "коли дозволяє сезон.",
  "dashboard.comfort.humidity.label": "Комфортна вологість",
  "dashboard.comfort.humidity.help": "Підлаштовує витрату повітря, щоб вологість у приміщенні "
    + "лишалась комфортною.",
  "dashboard.comfort.protection.label": "Захист від вологості",
  "dashboard.comfort.protection.help": "Збільшує витрату повітря, щоб стримати вологість і "
    + "запобігти конденсату.",

  "dashboard.timers.title": "Таймери",
  "dashboard.timers.sub": "Скільки триває кожен тимчасовий режим. Задайте до запуску; установка "
    + "відлічує час сама.",
  "dashboard.timers.boost.running": "Інтенсивний режим",
  "dashboard.timers.boost.what": "Скільки інтенсивний режим працює на повній витраті, перш ніж "
    + "установка повернеться до розкладу.",
  "dashboard.timers.boost.idle": "Задає тривалість наступного інтенсивного режиму.",
  "dashboard.timers.bypass.running": "Примусовий байпас",
  "dashboard.timers.bypass.what": "Скільки діє ручне «Відкрито» чи «Закрито», перш ніж байпас "
    + "поверне керування в «Авто».",
  "dashboard.timers.bypass.idle": "Задає, скільки діє ручне керування байпасом.",
  "dashboard.timers.away.running": "Режим «Не вдома»",
  "dashboard.timers.away.what": "Ігнорує будь-який розклад, поки вдома порожньо — для відпустки, "
    + "а не швидкий пресет «Не вдома» вище. Без кінця він діє, доки ви його не завершите.",
  "dashboard.timers.away.idle": "Задає, скільки триватиме режим «Не вдома» після запуску.",
  "dashboard.timers.noEnd": "Кінець не задано",
  "dashboard.timers.runsUntil": "{mode}: діє, доки ви не завершите",
  "dashboard.timers.left": "лишилось {time}",
  "dashboard.timers.isRunning": "{mode}: працює",
  "dashboard.timers.notRunning": "Не активний",
  "dashboard.timers.endNow": "Завершити зараз",
  "dashboard.timers.startAway": "Почати «Не вдома»",
  "dashboard.timers.untilEnd": "Доки не завершу",
  "dashboard.timers.inMinutes": "{name} у хвилинах",
  "dashboard.timers.min": "хв",

  "dashboard.dur.indefinite": "Безстроково",
  "dashboard.dur.min": "{n} хв",
  "dashboard.dur.days": {
    one: "{count} день", few: "{count} дні", many: "{count} днів", other: "{count} дня",
  },
  "dashboard.dur.h": "{h} год",
  "dashboard.dur.hm": "{h} год {m} хв",
  "dashboard.dur.dh": "{d} д {h} год",
  "dashboard.dur.s": "{n} с",

  "dashboard.toast.notApplied": "Не застосовано: {msg}",

  "dashboard.guide.title": "Як користуватися вентиляцією",
  "dashboard.guide.lede": "Втручатися доводиться рідко — але ось коли кожне керування справді "
    + "знадобиться.",
  "dashboard.guide.auto.title": "Залиште на «Авто»",
  "dashboard.guide.auto.body": "Установка вимірює вологість і якість повітря та сама задає "
    + "витрату. Майже завжди це правильний вибір.",
  "dashboard.guide.away.title": "«Не вдома», коли в домі порожньо",
  "dashboard.guide.away.body": "Знижує до мінімуму, який не дає повітрю застоятися, і заощаджує "
    + "енергію. Так і лишається, доки ви не завершите.",
  "dashboard.guide.manual.title": "Низький · Середній · Високий вручну",
  "dashboard.guide.manual.body": "Задайте фоновий рівень самі, коли захочете: тихо вночі, "
    + "щоденно або більше повітря для людної чи вологої кімнати.",
  "dashboard.guide.boost.title": "Інтенсивний режим, щоб швидко провітрити",
  "dashboard.guide.boost.body": "Повна витрата на заданий час — від запахів кухні чи пари у "
    + "ванній — а потім установка сама повертається до розкладу.",
  "dashboard.guide.bypass.title": "Байпас охолоджує теплими ночами",
  "dashboard.guide.bypass.body": "Коли надворі прохолодніше, ніж усередині, установка спрямовує "
    + "свіже повітря повз теплообмінник, щоб охолодити дім. Вона робить це сама.",
  "dashboard.guide.filters.title": "Міняйте фільтри разом",
  "dashboard.guide.filters.body": "Коли лічильник добіжить кінця, замініть обидва фільтри одразу. "
    + "Чисті фільтри — це чистіше повітря і тихіший, ефективніший вентилятор.",
  "dashboard.guide.about": "Як читати цю панель",
  "dashboard.guide.shape.term": "За формою",
  "dashboard.guide.shape.em": "довжина, положення або маршрут",
  "dashboard.guide.shape.body": "Кожна величина показана не лише кольором, а й як {em} — великий "
    + "показник, смуга рекуперації, маршрут байпаса, ресурс фільтра і кожне кільце відліку "
    + "читаються й без розрізнення кольорів.",
  "dashboard.guide.unit.term": "За установкою",
  "dashboard.guide.unit.em": "лише те, що повідомляє сама вентиляційна установка",
  "dashboard.guide.unit.body": "Вона показує {em}, а її елементи керування записують напряму в "
    + "установку через той самий API, що й будь-який інший клієнт.",
};
