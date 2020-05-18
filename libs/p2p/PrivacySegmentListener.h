#ifndef PRIVACYSEGMENTLISTENER_H
#define PRIVACYSEGMENTLISTENER_H

class PrivacySegmentListener
{
public:
    inline PrivacySegmentListener() {}
    virtual ~PrivacySegmentListener();

    virtual void filterUpdated() = 0;
};

#endif
