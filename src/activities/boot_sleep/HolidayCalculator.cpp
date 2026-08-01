#include "HolidayCalculator.h"

// The HOLIDAY_TABLE below is deliberately declared with static-storage
// initialisation using string literals only. All entries end up in Flash
// (Instruction Bus, DRAM-free) per the "static const lookup table" rule in
// CLAUDE.md. sizeof(HolidayEntry) is 12 bytes, so the whole table is 156B.
//
// String literals are written in UTF-8 directly (á, é, í, ó, ú, ñ, ¡). The
// firmware's Noto Sans fonts include Latin-1 Supplement so these render on
// the panel; the rendering pipeline treats these as opaque byte sequences.
namespace calendar {

const HolidayEntry HOLIDAY_TABLE[static_cast<size_t>(HolidayId::HolidayIdCount)] = {
    // label, rule, month, nth/day, weekday, dayOffset, regions
    /* AnoNuevo            */ {"Año Nuevo", HolidayRule::FixedDate, 1, 1, 0, 0, REGION_CR | REGION_US},
    /* DiaDeMlk            */ {"Día de Martin Luther King", HolidayRule::NthWeekday, 1, 3, 1, 0, REGION_US},
    /* DiaDeLosPresidentes */ {"Día de los Presidentes", HolidayRule::NthWeekday, 2, 3, 1, 0, REGION_US},
    /* JuevesSanto         */ {"Jueves Santo", HolidayRule::EasterRelative, 0, 0, 0, -3, REGION_CR},
    /* ViernesSanto        */ {"Viernes Santo", HolidayRule::EasterRelative, 0, 0, 0, -2, REGION_CR},
    /* JuanSantamaria      */ {"Día de Juan Santamaría", HolidayRule::FixedDate, 4, 11, 0, 0, REGION_CR},
    /* DiaDelTrabajoCR     */ {"Día del Trabajo", HolidayRule::FixedDate, 5, 1, 0, 0, REGION_CR},
    /* DiaDeLosCaidos      */ {"Día de los Caídos", HolidayRule::NthWeekday, 5, -1, 1, 0, REGION_US},
    /* Juneteenth          */ {"Juneteenth", HolidayRule::FixedDate, 6, 19, 0, 0, REGION_US},
    /* IndependenciaUS     */ {"Independencia de EE.UU.", HolidayRule::FixedDate, 7, 4, 0, 0, REGION_US},
    /* AnexionNicoya       */ {"Anexión del Partido de Nicoya", HolidayRule::FixedDate, 7, 25, 0, 0, REGION_CR},
    /* VirgenDeLosAngeles  */ {"Día de la Virgen de los Ángeles", HolidayRule::FixedDate, 8, 2, 0, 0, REGION_CR},
    /* DiaDeLaMadre        */ {"Día de la Madre", HolidayRule::FixedDate, 8, 15, 0, 0, REGION_CR},
    /* DiaDeLaPersonaNegra */ {"Día de la Persona Negra", HolidayRule::FixedDate, 8, 31, 0, 0, REGION_CR},
    /* DiaDelTrabajoUS     */ {"Día del Trabajo (EE.UU.)", HolidayRule::NthWeekday, 9, 1, 1, 0, REGION_US},
    /* IndependenciaCR     */ {"Día de la Independencia", HolidayRule::FixedDate, 9, 15, 0, 0, REGION_CR},
    /* PueblosIndigenas    */ {"Día de los Pueblos Indígenas", HolidayRule::NthWeekday, 10, 2, 1, 0, REGION_US},
    /* EncuentroCulturas   */ {"Encuentro de las Culturas", HolidayRule::FixedDate, 10, 12, 0, 0, REGION_CR},
    /* DiaDeLosVeteranos   */ {"Día de los Veteranos", HolidayRule::FixedDate, 11, 11, 0, 0, REGION_US},
    /* AccionDeGracias     */ {"Acción de Gracias", HolidayRule::NthWeekday, 11, 4, 4, 0, REGION_US},
    /* ViernesNegro        */ {"Viernes Negro", HolidayRule::NthWeekday, 11, 4, 4, 1, REGION_US},
    /* AbolicionEjercito   */ {"Abolición del Ejército", HolidayRule::FixedDate, 12, 1, 0, 0, REGION_CR},
    /* NocheBuena          */ {"Noche Buena", HolidayRule::FixedDate, 12, 24, 0, 0, REGION_US},
    /* Navidad             */ {"Navidad", HolidayRule::FixedDate, 12, 25, 0, 0, REGION_CR | REGION_US},
};

uint8_t holidayRegions(HolidayId id) {
  return HOLIDAY_TABLE[static_cast<size_t>(id)].regions;
}

// Zeller's Congruence, standardised to 0=Sun..6=Sat.
uint8_t weekdayOf(uint16_t year, uint8_t month, uint8_t day) {
  int m = month;
  int y = year;
  if (m < 3) {
    m += 12;
    --y;
  }
  const int K = y % 100;
  const int J = y / 100;
  int h = day + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 - 2 * J;
  h = ((h % 7) + 7) % 7;
  return static_cast<uint8_t>((h + 6) % 7);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return DAYS[month - 1];
}

bool isValidDate(uint16_t year, uint8_t month, uint8_t day) {
  if (month < 1 || month > 12) return false;
  if (day < 1) return false;
  return day <= daysInMonth(year, month);
}

void addDays(YMD& date, int32_t days) {
  if (days > 0) {
    while (days > 0) {
      const uint8_t dim = daysInMonth(date.year, date.month);
      const uint32_t remainInMonth = dim - date.day;
      if (static_cast<uint32_t>(days) <= remainInMonth) {
        date.day = static_cast<uint8_t>(date.day + days);
        return;
      }
      days -= static_cast<int32_t>(remainInMonth + 1);
      date.day = 1;
      ++date.month;
      if (date.month > 12) {
        date.month = 1;
        ++date.year;
      }
    }
  } else if (days < 0) {
    while (days < 0) {
      if (static_cast<int32_t>(date.day) + days >= 1) {
        date.day = static_cast<uint8_t>(static_cast<int32_t>(date.day) + days);
        return;
      }
      days += date.day;
      if (date.month == 1) {
        date.month = 12;
        --date.year;
      } else {
        --date.month;
      }
      date.day = daysInMonth(date.year, date.month);
    }
  }
}

// Anonymous Gregorian Easter algorithm (Meeus/Jones/Butcher). All arithmetic
// is integer division; the algorithm produces the Gregorian date of Easter
// Sunday for any year >= 1583.
YMD computeEaster(uint16_t year) {
  const int Y = year;
  const int a = Y % 19;
  const int b = Y / 100;
  const int c = Y % 100;
  const int d = b / 4;
  const int e = b % 4;
  const int f = (b + 8) / 25;
  const int g = (b - f + 1) / 3;
  const int h = (19 * a + b - d - g + 15) % 30;
  const int i = c / 4;
  const int k = c % 4;
  const int L = (32 + 2 * e + 2 * i - h - k) % 7;
  const int m = (a + 11 * h + 22 * L) / 451;
  const int month = (h + L - 7 * m + 114) / 31;
  const int day = ((h + L - 7 * m + 114) % 31) + 1;
  return YMD{year, static_cast<uint8_t>(month), static_cast<uint8_t>(day)};
}

static YMD resolveNthWeekday(uint16_t year, uint8_t month, int8_t nth, int8_t weekday) {
  const uint8_t dim = daysInMonth(year, month);
  if (nth == -1) {
    for (uint8_t d = dim; d >= 1; --d) {
      if (weekdayOf(year, month, d) == static_cast<uint8_t>(weekday)) {
        return YMD{year, month, d};
      }
    }
  } else {
    int count = 0;
    for (uint8_t d = 1; d <= dim; ++d) {
      if (weekdayOf(year, month, d) == static_cast<uint8_t>(weekday)) {
        if (++count == nth) return YMD{year, month, d};
      }
    }
  }
  return YMD{year, month, 1};
}

YMD resolveHoliday(HolidayId id, uint16_t year) {
  const auto idx = static_cast<size_t>(id);
  const HolidayEntry& e = HOLIDAY_TABLE[idx];
  YMD out{};
  switch (e.rule) {
    case HolidayRule::FixedDate:
      out = YMD{year, e.month, static_cast<uint8_t>(e.nth)};
      break;
    case HolidayRule::NthWeekday:
      out = resolveNthWeekday(year, e.month, e.nth, e.weekday);
      break;
    case HolidayRule::EasterRelative:
      out = computeEaster(year);
      break;
  }
  if (e.dayOffset != 0) {
    addDays(out, e.dayOffset);
  }
  return out;
}

YMD localDateFromUtc(const YMD& utcDate, uint8_t utcHour, uint8_t utcMinute, uint8_t offsetQuarterHoursBiased) {
  // Same clamp as HalClock::formatTime: a corrupted setting must not shift the
  // date by a wild amount.
  uint8_t offsetQ = offsetQuarterHoursBiased;
  if (offsetQ > 104) offsetQ = 48;  // 48 == UTC+0

  YMD local = utcDate;
  const int32_t localMinutes =
      static_cast<int32_t>(utcHour) * 60 + utcMinute + (static_cast<int32_t>(offsetQ) - 48) * 15;
  if (localMinutes < 0) {
    addDays(local, -1);
  } else if (localMinutes >= 24 * 60) {
    addDays(local, 1);
  }
  return local;
}

}  // namespace calendar
