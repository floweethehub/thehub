#ifndef FLOWEE_TEST_H
#define FLOWEE_TEST_H
#include <QObject>
#include <QtTest/QtTest>

namespace Test
{
class Flowee : public QObject {
    Q_OBJECT
public:
    Flowee();

private:
    const char *currentTestName();
    char m_currentTestname[40];
    const char *m_prevTest = nullptr;
};
}

#endif
