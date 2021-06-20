/* This file is part of Flowee
 *
 * Copyright (C) 2017 Nathan Osman
 * Copyright (C) 2019 Tom Zander <tom@flowee.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * For the full copy of the License see <http://www.gnu.org/licenses/>
 */

#ifndef HTTPENGINE_SOCKET_H
#define HTTPENGINE_SOCKET_H

#include <QHostAddress>
#include <QIODevice>
#include <QJsonDocument>
#include <QMultiMap>

#include "ibytearray.h"

#include "httpengine_export.h"

class QJsonDocument;
class QTcpSocket;

namespace HttpEngine
{

class HTTPENGINE_EXPORT SocketPrivate;

/**
 * @brief Implementation of the HTTP protocol
 *
 * This class provides a class derived from QIODevice that can be used to read
 * data from and write data to an HTTP client through a QTcpSocket provided in
 * the constructor. The socket will assume ownership of the QTcpSocket and
 * ensure it is properly deleted. Consequently, the QTcpSocket must have been
 * allocated on the heap:
 *
 * @code
 * QTcpSocket *tcpSock = new QTcpSocket;
 * tcpSock->connectToHost(...);
 * tcpSock->waitForConnected();
 *
 * HttpEngine::Socket *httpSock = new QHttpEngine::Socket(tcpSock);
 * @endcode
 *
 * Once the headersParsed() signal is emitted, information about the request
 * can be retrieved using the appropriate methods. As data is received, the
 * readyRead() signal is emitted and any available data can be read using
 * QIODevice::read():
 *
 * @code
 * QByteArray data;
 * connect(httpSock, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
 *
 * void MyClass::onReadyRead()
 * {
 *     data.append(httpSock->readAll());
 * }
 * @endcode
 *
 * If the client sets the `Content-Length` header, the readChannelFinished()
 * signal will be emitted when the specified amount of data is read from the
 * client. Otherwise the readChannelFinished() signal will be emitted
 * immediately after the headers are read.
 *
 * The status code and headers may be set as long as no data has been written
 * to the device and the writeHeaders() method has not been called. The
 * headers are written either when the writeHeaders() method is called or when
 * data is first written to the device:
 *
 * @code
 * httpSock->setStatusCode(HttpSocket::OK);
 * httpSock->setHeader("Content-Length", 13);
 * httpSock->write("Hello, world!");
 * @endcode
 *
 * This class also provides methods that simplify writing a redirect or an
 * HTTP error to the socket. To write a redirect, simply pass a path to the
 * writeRedirect() method. To write an error, simply pass the desired HTTP
 * status code to the writeError() method. Both methods will close the socket
 * once the response is written.
 */
class HTTPENGINE_EXPORT Socket : public QIODevice
{
    Q_OBJECT

public:

    /**
     * @brief Map consisting of query string values
     */
    typedef QMultiMap<QString, QString> QueryStringMap;

    /**
     * @brief Map consisting of HTTP headers
     *
     * The key used for the map is the
     * [IByteArray](@ref HttpEngine::IByteArray) class, which allows for
     * case-insensitive comparison.
     */
    typedef QMultiMap<IByteArray, QByteArray> HeaderMap;

    /**
     * HTTP methods
     *
     * An integer constant is provided for each of the methods described in
     * RFC 2616 (HTTP/1.1).
     */
    enum Method {
        /// Request for communications options
        OPTIONS = 1,
        /// Request resource
        GET = 1 << 1,
        /// Request resource without body
        HEAD = 1 << 2,
        /// Store subordinate resource
        POST = 1 << 3,
        /// Store resource
        PUT = 1 << 4,
        /// Delete resource
        DELETE = 1 << 5,
        /// Diagnostic trace
        TRACE = 1 << 6,
        /// Proxy connection
        CONNECT = 1 << 7
    };

    /**
     * Predefined constants for HTTP status codes
     */
    enum {
        /// Request was successful
        OK = 200,
        /// Request was successful and a resource was created
        Created = 201,
        /// Request was accepted for processing, not completed yet.
        Accepted = 202,
        /// %Range request was successful
        PartialContent = 206,
        /// Resource has moved permanently
        MovedPermanently = 301,
        /// Resource is available at an alternate URI
        Found = 302,
        /// Bad client request
        BadRequest = 400,
        /// Client is unauthorized to access the resource
        Unauthorized = 401,
        /// Access to the resource is forbidden
        Forbidden = 403,
        /// Resource was not found
        NotFound = 404,
        /// Method is not valid for the resource
        MethodNotAllowed = 405,
        /// The request could not be completed due to a conflict with the current state of the resource
        Conflict = 409,
        /// An internal server error occurred
        InternalServerError = 500,
        /// Invalid response from server while acting as a gateway
        BadGateway = 502,
        /// %Server unable to handle request due to overload
        ServiceUnavailable = 503,
        /// %Server does not supports the HTTP version in the request
        HttpVersionNotSupported = 505
    };

    /**
     * @brief Create a new socket from a QTcpSocket
     *
     * This instance will assume ownership of the QTcpSocket. That is, it will
     * make itself the parent of the socket.
     */
    Socket(QTcpSocket *socket, QObject *parent = nullptr);

    /**
     * @brief Retrieve the number of bytes available for reading
     *
     * This method indicates the number of bytes that could immediately be
     * read by a call to QIODevice::readAll().
     */
    qint64 bytesAvailable() const override;

    /**
     * @brief Determine if the device is sequential
     *
     * This method will always return true.
     */
    bool isSequential() const override;

    /**
     * @brief Close the device and underlying socket
     *
     * Invoking this method signifies that no more data will be written to the
     * device. It will also close the underlying QTcpSocket and destroy this
     * object.
     */
    void close() override;

    /**
     * @brief Retrive the address of the remote peer
     */
    QHostAddress peerAddress() const;

    /**
     * @brief Determine if the request headers have been parsed yet
     */
    bool isHeadersParsed() const;

    /**
     * @brief Retrieve the request method
     *
     * This method may only be called after the request headers have been
     * parsed.
     */
    Method method() const;

    /**
     * @brief Retrieve the raw request path
     *
     * This method may only be called after the request headers have been
     * parsed.
     */
    QByteArray rawPath() const;

    /**
     * @brief Retrieve the decoded path with the query string removed
     *
     * This method may only be called after the request headers have been
     * parsed.
     */
    QString path() const;

    /**
     * @brief Retrieve the query string
     *
     * This method may only be called after the request headers have been
     * parsed.
     */
    QueryStringMap queryString() const;

    /**
     * @brief Retrieve a map of request headers
     *
     * This method may only be called after the request headers have been
     * parsed. The original case of the headers is preserved but comparisons
     * are performed in a case-insensitive manner.
     */
    HeaderMap headers() const;

    /**
     * @brief Retrieve the length of the content
     *
     * This value is provided by the `Content-Length` HTTP header (if present)
     * and returns -1 if the value is not available.
     */
    qint64 contentLength() const;

    /**
     * @brief Parse the request body as a JSON document
     *
     * This method may only be called after the request headers **and** the
     * request body have been received. The most effective way to confirm that
     * this is the case is by using:
     *
     * @code
     * socket->bytesAvailable() >= socket->contentLength()
     * @endcode
     *
     * If the JSON received is invalid, an error will be immediately written
     * to the socket. The return value indicates whether the JSON was valid.
     */
    bool readJson(QJsonDocument &document);

    /**
     * @brief Set the response code
     *
     * This method may only be called before the response headers are written.
     *
     * The statusReason parameter may be omitted if one of the predefined
     * status code constants is used. If no response status code is explicitly
     * set, it will assume a default value of "200 OK".
     */
    void setStatusCode(int statusCode, const QByteArray &statusReason = QByteArray());

    /**
     * @brief Set a response header to a specific value
     *
     * This method may only be called before the response headers are written.
     * Duplicate values will be either appended to the header or used to
     * replace the original value, depending on the third parameter.
     */
    void setHeader(const QByteArray &name, const QByteArray &value, bool replace = true);

    /**
     * @brief Set the response headers
     *
     * This method may only be called before the response headers are written.
     * All existing headers will be overwritten.
     */
    void setHeaders(const HeaderMap &headers);

    /**
     * @brief Write response headers to the socket
     *
     * This method should not be invoked after the response headers have been
     * written.
     */
    void writeHeaders();

    /**
     * @brief Write an HTTP 3xx redirect to the socket and close it
     */
    void writeRedirect(const QByteArray &path, bool permanent = false);

    /**
     * @brief Write an HTTP error to the socket and close it
     */
    void writeError(int statusCode, const QByteArray &statusReason = QByteArray());

    /**
     * @brief Write the specified JSON document to the socket and close it
     */
    void writeJson(const QJsonDocument &document, QJsonDocument::JsonFormat format = QJsonDocument::Indented);

Q_SIGNALS:

    /**
     * @brief Indicate that request headers have been parsed
     *
     * This signal is emitted when the request headers have been received from
     * the client and parsing is complete. It is then safe to begin reading
     * request data. The readyRead() signal will be emitted as request data is
     * received.
     */
    void headersParsed();

    /**
     * @brief Indicate that the client has disconnected
     */
    void disconnected();

protected:

    /**
     * @brief Implementation of QIODevice::readData()
     */
    qint64 readData(char *data, qint64 maxlen) override;

    /**
     * @brief Implementation of QIODevice::writeData()
     */
    qint64 writeData(const char *data, qint64 len) override;

private:

    SocketPrivate *const d;
    friend class SocketPrivate;
};


/**
 * Return from the QRC the template with error replacement string.
 */
void HTTPENGINE_EXPORT returnTemplatePath(HttpEngine::Socket *socket, const QString &templateName, const QString &error = QString());
}

#endif
