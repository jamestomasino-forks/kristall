#include "geminiclient.hpp"
#include <cassert>
#include <QDebug>
#include <QSslConfiguration>

GeminiClient::GeminiClient(QObject *parent) : QObject(parent)
{
    connect(&socket, &QSslSocket::encrypted, this, &GeminiClient::socketEncrypted);
    connect(&socket, &QSslSocket::readyRead, this, &GeminiClient::socketReadyRead);
    connect(&socket, &QSslSocket::disconnected, this, &GeminiClient::socketDisconnected);
    connect(&socket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors), this, &GeminiClient::sslErrors);
    connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QSslSocket::error), this, &GeminiClient::socketError);


    QSslConfiguration ssl_config;
    ssl_config.setProtocol(QSsl::TlsV1_2);
    // ssl_config.setLocalCertificate(QSslCertificate::1

    socket.setSslConfiguration(ssl_config);

}

GeminiClient::~GeminiClient()
{
    is_receiving_body = false;
}

bool GeminiClient::startRequest(const QUrl &url)
{
    if(url.scheme() != "gemini")
        return false;

    if(socket.isOpen())
        return false;

    socket.connectToHostEncrypted(url.host(), url.port(1965));

    buffer.clear();
    body.clear();
    is_receiving_body = false;

    if(not socket.isOpen())
        return false;

    target_url = url;
    mime_type = "<invalid>";

    return true;
}

bool GeminiClient::isInProgress() const
{
    return socket.isOpen();
}

bool GeminiClient::cancelRequest()
{
    this->is_receiving_body = false;
    this->socket.close();
    this->buffer.clear();
    this->body.clear();
    return true;
}

void GeminiClient::enableClientCertificate(const CryptoIdentity &ident)
{
    this->socket.setLocalCertificate(ident.certificate);
    this->socket.setPrivateKey(ident.private_key);
}

void GeminiClient::disableClientCertificate()
{
    this->socket.setLocalCertificate(QSslCertificate{});
    this->socket.setPrivateKey(QSslKey { });
}

void GeminiClient::socketEncrypted()
{
    QString request = target_url.toString(QUrl::FormattingOptions(QUrl::FullyEncoded)) + "\r\n";

    QByteArray request_bytes = request.toUtf8();

    qint64 offset = 0;
    while(offset < request_bytes.size()) {
        auto const len = socket.write(request_bytes.constData() + offset, request_bytes.size() - offset);
        if(len <= 0)
        {
            socket.close();
            return;
        }
        offset += len;
    }
}

void GeminiClient::socketReadyRead()
{
    QByteArray response = socket.readAll();

    if(is_receiving_body)
    {
        body.append(response);
        emit this->requestProgress(body.size());
    }
    else
    {
        for(int i = 0; i < response.size(); i++)
        {
            if(response[i] == '\n') {
                buffer.append(response.data(), i);
                body.append(response.data() + i + 1, response.size() - i - 1);

                // "XY " <META> <CR> <LF>
                if(buffer.size() <= 5) {
                    socket.close();
                    qDebug() << buffer;
                    emit protocolViolation("Line is too short for valid protocol");
                    return;
                }
                if(buffer[buffer.size() - 1] != '\r') {
                    socket.close();
                    qDebug() << buffer;
                    emit protocolViolation("Line does not end with <CR> <LF>");
                    return;
                }
                if(not isdigit(buffer[0])) {
                    socket.close();
                    qDebug() << buffer;
                    emit protocolViolation("First character is not a digit.");
                    return;
                }
                if(not isdigit(buffer[1])) {
                    socket.close();
                    qDebug() << buffer;
                    emit protocolViolation("Second character is not a digit.");
                    return;
                }
                // TODO: Implement stricter version
                // if(buffer[2] != ' ') {
                if(not isspace(buffer[2])) {
                    socket.close();
                    qDebug() << buffer;
                    emit protocolViolation("Third character is not a space.");
                    return;
                }

                QString meta = QString::fromUtf8(buffer.data() + 3, buffer.size() - 4);

                int primary_code = buffer[0] - '0';
                int secondary_code = buffer[1] - '0';

                qDebug() << primary_code << secondary_code << meta;

                // We don't need to receive any data after that.
                if(primary_code != 2)
                    socket.close();

                switch(primary_code)
                {
                case 1: // requesting input
                    emit inputRequired(meta);
                    return;

                case 2: // success
                    is_receiving_body = true;
                    mime_type = meta;
                    return;

                case 3: { // redirect
                    QUrl new_url(meta);
                    if(new_url.isValid()) {
                        if(new_url.isRelative())
                            new_url =  target_url.resolved(new_url);
                        assert(not new_url.isRelative());

                        emit redirected(new_url, (secondary_code == 1));
                    }
                    else {
                        emit protocolViolation("Invalid URL for redirection!");
                    }
                    return;
                }

                case 4: { // temporary failure
                    TemporaryFailure type = TemporaryFailure::unspecified;
                    switch(secondary_code)
                    {
                    case 1: type = TemporaryFailure::server_unavailable; break;
                    case 2: type = TemporaryFailure::cgi_error; break;
                    case 3: type = TemporaryFailure::proxy_error; break;
                    case 4: type = TemporaryFailure::slow_down; break;
                    }
                    emit temporaryFailure(type, meta);
                    return;
                }

                case 5: { // permanent failure
                    PermanentFailure type = PermanentFailure::unspecified;
                    switch(secondary_code)
                    {
                    case 1: type = PermanentFailure::not_found; break;
                    case 2: type = PermanentFailure::gone; break;
                    case 3: type = PermanentFailure::proxy_request_required; break;
                    case 9: type = PermanentFailure::bad_request; break;
                    }
                    emit permanentFailure(type, meta);
                    return;
                }

                case 6: // client certificate required
                    switch(secondary_code)
                    {
                    case 1:
                        emit transientCertificateRequested(meta);
                        return;

                    case 2:
                        emit authorisedCertificateRequested(meta);
                        return;

                    case 3:
                        emit certificateRejected(CertificateRejection::not_accepted, meta);
                        return;

                    case 4:
                        emit certificateRejected(CertificateRejection::future_certificate_rejected, meta);
                        return;

                    case 5:
                        emit certificateRejected(CertificateRejection::expired_certificate_rejected, meta);
                        return;

                    default:
                        emit certificateRejected(CertificateRejection::unspecified, meta);
                        return;
                    }
                    return;

                default:
                    emit protocolViolation("Unspecified status code used!");
                    return;
                }

                assert(false and "unreachable");
            }
        }
        buffer.append(response);
    }
}

void GeminiClient::socketDisconnected()
{
    if(is_receiving_body) {
        body.append(socket.readAll());
        emit requestComplete(body, mime_type);
    }
}

void GeminiClient::sslErrors(const QList<QSslError> &errors)
{
    for(auto const & error : errors) {
        qWarning() << error.errorString() ;
    }

    socket.ignoreSslErrors(errors);
}

void GeminiClient::socketError(QAbstractSocket::SocketError socketError)
{
    // When remote host closes TLS session, the client closes the socket.
    // This is more sane then erroring out here as it's a perfectly legal
    // state and we know the TLS connection has ended.
    if(socketError == QAbstractSocket::RemoteHostClosedError) {
        socket.close();
    } else {
        qWarning() << socketError << socket.errorString();
    }
}
