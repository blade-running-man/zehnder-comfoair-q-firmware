"""DE/NL/UK translations of the entity display names, keyed by entity key.

The English name is the canonical one and lives in pdo_table.py: it is what reaches Home
Assistant through discovery, and four of them are frozen by the HA integration's contract. This
file adds ONLY the other three languages, for the web UI's language switcher. Nothing here
reaches the firmware or discovery, so Home Assistant stays English whatever the SPA shows.

This is a second table keyed by entity key, so it CAN drift from pdo_table.py. It does not,
because render_entity_names_ts.py fails generation (and with it the firmware build) unless the
key set here is exactly the registry's -- no missing key, no orphan, no empty name. Add an
entity to pdo_table.py and the build tells you to add its three translations here. DO NOT add
an "en" column: a second English name would be a second, silently divergent entity list.

Vocabulary, kept consistent across rows (the unit's own manuals use these):
    outdoor air  -> Außenluft / buitenlucht / зовнішнє повітря   (outside -> unit)
    supply air   -> Zuluft / toevoerlucht / припливне повітря      (unit -> rooms)
    extract air  -> Abluft / afvoerlucht / витяжне повітря         (rooms -> unit)
    exhaust air  -> Fortluft / uitblaaslucht / викидне повітря     (unit -> outside)
The registry's `exhaust_fan_*` keys are named "Extract" in English -- the fan that pulls air
out of the rooms -- and are translated as extract, following the English name, not the key.
Draft wording; the owner reviews it.
"""
from __future__ import annotations

LOCALES = ("de", "nl", "uk")

# key -> {"de": ..., "nl": ..., "uk": ...}. English is pdo_table.py's `name`.
ENTITY_NAMES_I18N: dict[str, dict[str, str]] = {
    # --- frozen raw countdowns (English name == key, see pdo_table.py) ---
    "next_fan_change_in_raw": {"de": "Nächste Lüfteränderung in (roh)", "nl": "Volgende ventilatorwijziging over (ruw)", "uk": "Наступна зміна вентилятора через (сирі дані)"},
    "next_bypass_change_in_raw": {"de": "Nächste Bypass-Änderung in (roh)", "nl": "Volgende bypasswijziging over (ruw)", "uk": "Наступна зміна байпаса через (сирі дані)"},
    "next_supply_change_in_raw": {"de": "Nächste Zuluft-Änderung in (roh)", "nl": "Volgende toevoerwijziging over (ruw)", "uk": "Наступна зміна припливу через (сирі дані)"},
    "next_exhaust_change_in_raw": {"de": "Nächste Abluft-Änderung in (roh)", "nl": "Volgende afvoerwijziging over (ruw)", "uk": "Наступна зміна витяжки через (сирі дані)"},
    # --- fans ---
    "exhaust_fan_duty": {"de": "Abluftventilator Ansteuerung", "nl": "Afvoerventilator aansturing", "uk": "Навантаження витяжного вентилятора"},
    "supply_fan_duty": {"de": "Zuluftventilator Ansteuerung", "nl": "Toevoerventilator aansturing", "uk": "Навантаження припливного вентилятора"},
    "exhaust_fan_flow": {"de": "Abluftvolumenstrom", "nl": "Afvoerdebiet", "uk": "Потік витяжки"},
    "supply_fan_flow": {"de": "Zuluftvolumenstrom", "nl": "Toevoerdebiet", "uk": "Потік припливу"},
    "exhaust_fan_speed": {"de": "Abluftventilator Drehzahl", "nl": "Afvoerventilator toerental", "uk": "Оберти витяжного вентилятора"},
    "supply_fan_speed": {"de": "Zuluftventilator Drehzahl", "nl": "Toevoerventilator toerental", "uk": "Оберти припливного вентилятора"},
    # --- power and energy ---
    "power": {"de": "Leistung Lüftung", "nl": "Vermogen ventilatie", "uk": "Потужність вентиляції"},
    "energy_ytd": {"de": "Energie Lüftung (lfd. Jahr)", "nl": "Energie ventilatie (dit jaar)", "uk": "Енергія вентиляції (з початку року)"},
    "energy_total": {"de": "Energie Lüftung gesamt", "nl": "Energie ventilatie totaal", "uk": "Енергія вентиляції (усього)"},
    "preheater_energy_ytd": {"de": "Energie Vorheizregister (lfd. Jahr)", "nl": "Energie voorverwarmer (dit jaar)", "uk": "Енергія попереднього нагрівача (з початку року)"},
    "preheater_energy_total": {"de": "Energie Vorheizregister gesamt", "nl": "Energie voorverwarmer totaal", "uk": "Енергія попереднього нагрівача (усього)"},
    "preheater_power": {"de": "Leistung Vorheizregister", "nl": "Vermogen voorverwarmer", "uk": "Потужність попереднього нагрівача"},
    "filter_replacement_remaining_days": {"de": "Tage bis zum Filterwechsel", "nl": "Dagen tot filtervervanging", "uk": "Днів до заміни фільтрів"},
    "running_mean_outdoor_temp": {"de": "Gleitendes Mittel der Außentemperatur", "nl": "Voortschrijdend gemiddelde buitentemperatuur", "uk": "Ковзна середня зовнішня температура"},
    "profile_target_temp": {"de": "Zieltemperatur des Profils", "nl": "Doeltemperatuur profiel", "uk": "Цільова температура профілю"},
    "avoided_heating": {"de": "Vermiedene Heizleistung", "nl": "Vermeden verwarming", "uk": "Заощаджене опалення"},
    "avoided_heating_ytd": {"de": "Vermiedene Heizenergie (lfd. Jahr)", "nl": "Vermeden verwarming (dit jaar)", "uk": "Заощаджене опалення (з початку року)"},
    "avoided_heating_total": {"de": "Vermiedene Heizenergie gesamt", "nl": "Vermeden verwarming totaal", "uk": "Заощаджене опалення (усього)"},
    "avoided_cooling": {"de": "Vermiedene Kühlleistung", "nl": "Vermeden koeling", "uk": "Заощаджене охолодження"},
    "avoided_cooling_ytd": {"de": "Vermiedene Kühlenergie (lfd. Jahr)", "nl": "Vermeden koeling (dit jaar)", "uk": "Заощаджене охолодження (з початку року)"},
    "avoided_cooling_total": {"de": "Vermiedene Kühlenergie gesamt", "nl": "Vermeden koeling totaal", "uk": "Заощаджене охолодження (усього)"},
    # --- air temperatures and humidities ---
    "intake_air_temp_unverified": {"de": "Ansauglufttemperatur (ungeprüft)", "nl": "Aanzuigluchttemperatuur (niet geverifieerd)", "uk": "Температура забірного повітря (не перевірено)"},
    "supply_air_temp_post_heater": {"de": "Zulufttemperatur (Regelung)", "nl": "Toevoerluchttemperatuur (regeling)", "uk": "Температура припливного повітря (регулятор)"},
    "bypass_position": {"de": "Bypass-Stellung", "nl": "Bypassstand", "uk": "Положення байпаса"},
    "fan_speed_modulated": {"de": "Lüftergeschwindigkeit (moduliert)", "nl": "Ventilatorsnelheid (gemoduleerd)", "uk": "Швидкість вентилятора (модульована)"},
    "extract_air_temp": {"de": "Ablufttemperatur", "nl": "Afvoerluchttemperatuur", "uk": "Температура витяжного повітря"},
    "exhaust_air_temp": {"de": "Fortlufttemperatur", "nl": "Uitblaasluchttemperatuur", "uk": "Температура викидного повітря"},
    "outdoor_air_temp": {"de": "Außenlufttemperatur", "nl": "Buitenluchttemperatuur", "uk": "Температура зовнішнього повітря"},
    "preheated_outdoor_air_temp": {"de": "Außenlufttemperatur (nach Vorheizregister)", "nl": "Buitenluchttemperatuur (na voorverwarmer)", "uk": "Температура зовнішнього повітря (після попереднього нагрівача)"},
    "supply_air_temp": {"de": "Zulufttemperatur", "nl": "Toevoerluchttemperatuur", "uk": "Температура припливного повітря"},
    "extract_air_humidity": {"de": "Abluftfeuchte", "nl": "Afvoerluchtvochtigheid", "uk": "Вологість витяжного повітря"},
    "exhaust_air_humidity": {"de": "Fortluftfeuchte", "nl": "Uitblaasluchtvochtigheid", "uk": "Вологість викидного повітря"},
    "outdoor_air_humidity": {"de": "Außenluftfeuchte", "nl": "Buitenluchtvochtigheid", "uk": "Вологість зовнішнього повітря"},
    "preheated_outdoor_air_humidity": {"de": "Außenluftfeuchte (nach Vorheizregister)", "nl": "Buitenluchtvochtigheid (na voorverwarmer)", "uk": "Вологість зовнішнього повітря (після попереднього нагрівача)"},
    "supply_air_humidity": {"de": "Zuluftfeuchte", "nl": "Toevoerluchtvochtigheid", "uk": "Вологість припливного повітря"},
    # --- binary sensors ---
    "manual_mode": {"de": "Manueller Modus", "nl": "Handmatige modus", "uk": "Ручний режим"},
    "supply_only_active": {"de": "Nur Zuluft", "nl": "Alleen toevoer", "uk": "Лише приплив"},
    "exhaust_only_active": {"de": "Nur Abluft", "nl": "Alleen afvoer", "uk": "Лише витяжка"},
    "boost_timer_running": {"de": "Boost-Timer läuft", "nl": "Boosttimer actief", "uk": "Таймер Boost працює"},
    "away_timer_running": {"de": "Abwesenheits-Timer läuft", "nl": "Afwezigheidstimer actief", "uk": "Таймер відсутності працює"},
    "heating_season": {"de": "Heizperiode", "nl": "Stookseizoen", "uk": "Опалювальний сезон"},
    "cooling_season": {"de": "Kühlperiode", "nl": "Koelseizoen", "uk": "Сезон охолодження"},
    "bypass_override": {"de": "Bypass-Übersteuerung aktiv", "nl": "Bypass-overschrijving actief", "uk": "Примусовий режим байпаса активний"},
    # --- enums ---
    "device_state": {"de": "Gerätezustand", "nl": "Apparaatstatus", "uk": "Стан пристрою"},
    "changing_filters": {"de": "Filterstatus", "nl": "Filterstatus", "uk": "Стан фільтрів"},
    "operating_mode": {"de": "Betriebsart", "nl": "Bedrijfsmodus", "uk": "Режим роботи"},
    "supply_fan_mode": {"de": "Modus Zuluftventilator", "nl": "Modus toevoerventilator", "uk": "Режим припливного вентилятора"},
    "exhaust_fan_mode": {"de": "Modus Abluftventilator", "nl": "Modus afvoerventilator", "uk": "Режим витяжного вентилятора"},
    "fan_level": {"de": "Lüfterstufe", "nl": "Ventilatiestand", "uk": "Рівень вентиляції"},
    "temperature_unit": {"de": "Temperatureinheit des Geräts", "nl": "Temperatuureenheid apparaat", "uk": "Одиниця температури пристрою"},
    "airflow_unit": {"de": "Luftstromeinheit des Geräts", "nl": "Debieteenheid apparaat", "uk": "Одиниця потоку повітря пристрою"},
    # --- frost protection (pdo_table.py, same section) ---
    "frost_protection_preheater_power": {"de": "Frostschutz Vorheizregister-Leistung (ungeprüft)", "nl": "Vorstbeveiliging voorverwarmervermogen (niet geverifieerd)", "uk": "Захист від замерзання: потужність попереднього нагрівача (не перевірено)"},
    "frost_protection_unbalance": {"de": "Frostschutz-Ungleichgewicht", "nl": "Vorstbeveiliging onbalans", "uk": "Захист від замерзання: дисбаланс"},
    "sensor_ventilation_mode": {"de": "Sensorgesteuerte Lüftung (roh, ungeprüft)", "nl": "Sensorgestuurde ventilatie (ruw, niet geverifieerd)", "uk": "Вентиляція за датчиками (сирі дані, не перевірено)"},
    # --- special (PDO 230) ---
    "airflow_constraints": {"de": "Luftstrombegrenzungen", "nl": "Debietbeperkingen", "uk": "Обмеження потоку повітря"},
    # --- the unit's active errors (read over RMI, not a PDO) ---
    "unit_error": {"de": "Gerätefehler", "nl": "Apparaatstoring", "uk": "Помилка установки"},
    "active_errors": {"de": "Aktive Fehler", "nl": "Actieve storingen", "uk": "Активні помилки"},
    # --- controls ---
    "bypass_mode": {"de": "Bypass-Modus", "nl": "Bypassmodus", "uk": "Режим байпаса"},
    "temperature_profile": {"de": "Temperaturprofil", "nl": "Temperatuurprofiel", "uk": "Температурний профіль"},
    "balance_mode": {"de": "Luftbalance", "nl": "Luchtbalans", "uk": "Баланс повітря"},
    "temperature_passive": {"de": "Passive Temperaturregelung", "nl": "Passieve temperatuurregeling", "uk": "Пасивне керування температурою"},
    "humidity_comfort": {"de": "Feuchtekomfort", "nl": "Vochtcomfort", "uk": "Комфортна вологість"},
    "humidity_protection": {"de": "Feuchteschutz", "nl": "Vochtbescherming", "uk": "Захист від вологості"},
    "ventilation_mode": {"de": "Lüftungsmodus", "nl": "Ventilatiemodus", "uk": "Режим вентиляції"},
    "boost_duration": {"de": "Boost-Dauer", "nl": "Boostduur", "uk": "Тривалість інтенсивного режиму"},
    "bypass_duration": {"de": "Dauer der Bypass-Übersteuerung", "nl": "Duur bypass-overschrijving", "uk": "Тривалість примусового байпаса"},
    "balance_duration": {"de": "Dauer der Luftbalance", "nl": "Duur luchtbalans", "uk": "Тривалість балансу повітря"},
    "away_duration": {"de": "Abwesenheitsdauer (0 = unbegrenzt)", "nl": "Afwezigheidsduur (0 = onbeperkt)", "uk": "Тривалість «Не вдома» (0 = безстроково)"},
    "away_mode": {"de": "Abwesenheitsmodus", "nl": "Afwezigheidsmodus", "uk": "Режим «Не вдома»"},
    "fan": {"de": "Lüftung", "nl": "Ventilatie", "uk": "Вентиляція"},
    # --- diagnostics ---
    "schema_version": {"de": "Schemaversion", "nl": "Schemaversie", "uk": "Версія схеми"},
    # The synthetic row generate_registry.py declares itself (FRESHNESS_KEY), not pdo_table.py.
    "data_freshness": {"de": "Datenaktualität", "nl": "Actualiteit van gegevens", "uk": "Актуальність даних"},
}
