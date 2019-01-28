
#ifndef XWP_TIMESTAMP_H
#define XWP_TIMESTAMP_H

#include <memory>

#include "xwp/basetypes.h"

namespace XWP
{

/***************************************************************************
 *
 *  DateStamp
 *
 **************************************************************************/

class DateStamp
{
public:
    DateStamp(uint16_t year, uint8_t month, uint8_t day)
        : _year(year), _month(month), _day(day)
    { }

    DateStamp(const DateStamp &d)
        : _year(d._year), _month(d._month), _day(d._day)
    { }

    bool operator==(const DateStamp &d) const = delete;
    bool operator!=(const DateStamp &d) const = delete;
    bool operator<(const DateStamp &d) const = delete;
    bool operator>(const DateStamp &d) const = delete;

protected:
    uint16_t _year;
    uint8_t _month;
    uint8_t _day;
};


/***************************************************************************
 *
 *  TimeStamp
 *
 **************************************************************************/

class TimeStamp;
typedef shared_ptr<TimeStamp> PTimeStamp;
typedef set<PTimeStamp> TimeStampSet;

class TimeStamp : public DateStamp
{
public:
    TimeStamp(uint16_t year,
              uint8_t month,
              uint8_t day,
              uint8_t hours,
              uint8_t minutes,
              uint8_t seconds)
        : DateStamp(year,
                    month,
                    day),
          _hours(hours),
          _minutes(minutes),
          _seconds(seconds)
    { }

    TimeStamp(const TimeStamp &d)
        : DateStamp(d),
          _hours(d._hours),
          _minutes(d._minutes),
          _seconds(d._seconds)
    {
    }

    virtual string toString(bool fCompact = false) const;

    /**
     *  Attempts to construct a TimeStamp from the given date string, which must be in
     *  YYYY-MM-DD HH:MM:SS format. Returns the new instance. If the string cannot be
     *  parsed and pcszThrowOnError is not nullptr, an error message is thrown (in which % is replaced
     *  with the value). Otherwise we return nullptr.
     *  As a result, this can be safely used as a "test and create" function.
     */
    static PTimeStamp Create(const string &strDateTime,
                                 const char *pcszThrowOnError);

    /**
     *  Produces a single string with all the dates of the given set separated by strGlue.
     */
    static string Implode(const string &strGlue,
                          const TimeStampSet &set);

    /**
     *  Explodes the given string into the given set of PTimeStamps. The string is
     *  assumed to have the timestamps separated by strDelimiter; this then calls Create()
     *  on every such string particle with the given pcszThrowOnError. Returns the no. of
     *  items inserted.
     */
    static size_t Explode(const string &str,
                          const string &strDelimiter,
                          TimeStampSet &dtset,
                          const char *pcszThrowOnError);

protected:
    uint8_t _hours;        // 0-23
    uint8_t _minutes;       // 0-59
    uint8_t _seconds;       // 0-59
};

} // namespace XWP

#endif // XWP_TIMESTAMP_H
