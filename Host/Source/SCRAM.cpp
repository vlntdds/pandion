/**
 * This file is part of Pandion.
 *
 * Pandion is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Pandion is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Pandion.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Filename:    SCRAM.cpp
 * Author(s):   Dries Staelens
 * Copyright:   Copyright (c) 2009 Dries Staelens
 * Description: This file implements some helper methods for authenticating
 *              using the Salted Challenge Response (SCRAM) SASL mechanism.
 *              See http://tools.ietf.org/html/draft-ietf-sasl-scram-10
 */
#include "stdafx.h"
#include "SCRAM.h"
#include "Hash.h"
#include "UTF.h"
#include "Base64.h"

SCRAM::SCRAM()
{
	ParseSCRAMMessage("p=tls-server-end-point,,n=Chris Newman,r=ClientNonce");
    ParseSCRAMMessage("r=ClientNonceServerNonce,s=PxR/wv+epq,i=128");
    ParseSCRAMMessage("c=cD10bHMtc2VydmVyLWVuZC1wb2ludCwsy1hFtXOnZ+ySrQM6srFpl/77uqvtxrg7nBY1BetEr/g=,r=ClientNonceServerNonce,p=WxPv/siO5l+qxN4");
    ParseSCRAMMessage("v=WxPv/siO5l+qxN4");
}
SCRAM::~SCRAM()
{
}

STDMETHODIMP SCRAM::Initialize(BSTR ClientUsername, BSTR ClientPassword)
{
	try
	{
		m_ClientUsername = 
			m_sprep.SASLPrep(UTF::utf16to8(ClientUsername), false);
		m_ClientPassword = 
			m_sprep.SASLPrep(UTF::utf16to8(ClientPassword), false);

		if(m_ClientUsername.length() == 0 || m_ClientPassword.length() == 0)
		{
			return E_FAIL;
		}
	}
	catch(StringPrepException e)
	{
		return E_FAIL;
	}

	GenerateNewClientNonce();
	return S_OK;
}

STDMETHODIMP SCRAM::GenerateClientFirstMessage(BSTR* ClientFirstMessage)
{
	/* Channel bindings unsupported */
	m_ClientFirstMessage += "n,";

	/* Client username */
	m_ClientFirstMessage += "n=";
	m_ClientFirstMessage += m_ClientUsername;
	m_ClientFirstMessage += ",";

	/* Client nonce */
	m_ClientFirstMessage += "r=";
	m_ClientFirstMessage += m_ClientNonce;

	*ClientFirstMessage = 
		::SysAllocString(Base64::Encode(m_ClientFirstMessage, false).c_str());

	return S_OK;
}

STDMETHODIMP SCRAM::GenerateClientFinalMessage(BSTR* ClientFinalMessage)
{
	/* GS2 Header */
	m_ClientFinalMessage += "c=biwsCg==,";

	/* Client/Server nonce */
	m_ClientFinalMessage += "r=";
	m_ClientFinalMessage += m_ClientNonce;
	m_ClientFinalMessage += m_ServerNonce;

	/* Auth Message */
	m_AuthMessage += m_ClientFirstMessage;
	m_AuthMessage += ",";
	m_AuthMessage += m_ServerFirstMessage;
	m_AuthMessage += ",";
	m_AuthMessage += m_ClientFinalMessage;
	m_AuthMessage += ",";

	/* Client Proof */
	GenerateClientProof();

	m_ClientFinalMessage += "p=";
	m_ClientFinalMessage += 
		UTF::utf16to8(Base64::Encode(m_ClientProof, false));

	*ClientFinalMessage =
		::SysAllocString(Base64::Encode(m_ClientFinalMessage, false).c_str());
	return S_OK;
}


STDMETHODIMP SCRAM::ValidateServerFirstMessage(BSTR ServerFirstMessage)
{
	ByteVector v = 
		Base64::Decode(UTF::utf16to8(ServerFirstMessage));
	m_ServerFirstMessage = UTF8String(v.begin(), v.end());
	std::map<char, UTF8String> parsedMessage = 
		ParseSCRAMMessage(m_ServerFirstMessage);

	m_Salt = Base64::Decode(parsedMessage['s']);
	m_IterationCount = atoi(parsedMessage['i'].c_str());

	if(parsedMessage['r'].find(m_ClientNonce) == 0)
	{
		m_ServerNonce = parsedMessage['r'].substr(m_ClientNonce.length());
		return S_OK;
	}
	else
	{
		return E_FAIL;
	}
}

STDMETHODIMP SCRAM::ValidateServerFinalMessage(BSTR ServerFinalMessage)
{
	ByteVector v = Base64::Decode(UTF::utf16to8(ServerFinalMessage));
	m_ServerFinalMessage = UTF8String(v.begin(), v.end());
	std::map<char, UTF8String> parsedMessage = 
		ParseSCRAMMessage(m_ServerFinalMessage);

	ByteVector ServerSignature =
		Base64::Decode(parsedMessage['v']);

	if(ServerSignature.size() == m_ServerSignature.size())
	{
		for(unsigned i = 0; i < m_ServerSignature.size(); i++)
		{
			if(m_ServerSignature[i] != ServerSignature[i])
			{
				return E_FAIL;
			}
		}
		return S_OK;
	}
	else
	{
		return E_FAIL;
	}
}

ByteVector SCRAM::Hi(
	const std::string str,
	const ByteVector salt,
	const unsigned i)
{
	ByteVector firstSalt(salt.begin(), salt.end());
	firstSalt.push_back(0);
	firstSalt.push_back(0);
	firstSalt.push_back(0);
	firstSalt.push_back(1);
	ByteVector u = HMAC_SHA1(
		ByteVector(str.begin(), str.end()), firstSalt);
	ByteVector result = u;
	
	for(unsigned j = 1; j < i; j++)
	{
		u = HMAC_SHA1(ByteVector(str.begin(), str.end()), u);
		for(unsigned k = 0; k < u.size(); k++)
		{
			result[k] ^= u[k];
		}
	}

	return result;
}

void SCRAM::GenerateNewClientNonce()
{
	HCRYPTPROV provider;
	unsigned char randomBytes[24];
	::CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_FULL, 0);
	::CryptGenRandom(provider, 24, randomBytes);
	::CryptReleaseContext(provider, 0);
	m_ClientNonce = UTF::utf16to8(Base64::Encode(randomBytes, 24, false));
}

void SCRAM::GenerateClientProof()
{
	/* Proof of identity */
	ByteVector saltedPassword =
		Hi(m_ClientPassword, m_Salt, m_IterationCount);
	ByteVector clientKey =
		HMAC_SHA1(saltedPassword, "Client Key");
	ByteVector storedKey(20);
	Hash::SHA1(&clientKey[0], clientKey.size(), &storedKey[0]);
	ByteVector clientSignature = 
		HMAC_SHA1(storedKey, m_AuthMessage);

	m_ClientProof.clear();
	for(unsigned j = 0; j < storedKey.size(); ++j)
	{
		m_ClientProof.push_back(clientKey[j] ^ clientSignature[j]);
	}

	/* Calculate the server signature */
	ByteVector serverKey = 
		HMAC_SHA1(saltedPassword, "Server Key");
	m_ServerSignature = HMAC_SHA1(serverKey, m_AuthMessage);
}

ByteVector SCRAM::HMAC_SHA1(
	const std::string key,
	const std::string text)
{
	return SCRAM::HMAC_SHA1(
		ByteVector(key.begin(), key.end()),
		ByteVector(text.begin(), text.end()));
}

ByteVector SCRAM::HMAC_SHA1(
	const ByteVector key,
	const std::string text)
{
	return SCRAM::HMAC_SHA1(
		key,
		ByteVector(text.begin(), text.end()));
}

ByteVector SCRAM::HMAC_SHA1(
	const ByteVector key,
	const ByteVector text)
{
	const unsigned int B = 64, L = 20;
	const ByteVector ipad(B, 0x36);
	const ByteVector opad(B, 0x5C);
	ByteVector internalKey(key.begin(), key.end());

	if(internalKey.size() > B)
	{
		unsigned char digest[L];
		Hash::SHA1(reinterpret_cast<const unsigned char*>(&internalKey[0]),
			internalKey.size(), digest); 
		internalKey.assign(digest,
			digest + sizeof(digest) / sizeof(unsigned char));
	}

	for(unsigned int i = internalKey.size(); i < B; i++)
	{
		internalKey.push_back(0x00);
	}

	ByteVector keyXORipad, keyXORopad;
	for(unsigned int i = 0; i < B; i++)
	{
		keyXORipad.push_back(internalKey[i] ^ ipad[i]);
		keyXORopad.push_back(internalKey[i] ^ opad[i]);
	}

	ByteVector innerMessage(keyXORipad);
	for(unsigned int i = 0; i < text.size(); i++)
	{
		innerMessage.push_back(text[i]);
	}

	unsigned char innerDigest[L];
	Hash::SHA1(&innerMessage[0], innerMessage.size(), innerDigest);

	ByteVector outerMessage(keyXORopad);
	for(unsigned int i = 0; i < sizeof(innerDigest); i++)
	{
		outerMessage.push_back(innerDigest[i]);
	}

	unsigned char outerDigest[L];
	Hash::SHA1(&outerMessage[0], outerMessage.size(), outerDigest);

	return ByteVector(outerDigest,
		outerDigest + sizeof(outerDigest) / sizeof(unsigned char));
}

UTF8String SCRAM::EscapeString(UTF8String str)
{
	UTF8String escapedString;
	for(UTF8String::const_iterator i = str.begin(); i != str.end(); ++i)
	{
		if(*i == '=')
		{
			escapedString += "=3D";
		}
		else if(*i == ',')
		{
			escapedString += "=2C";
		}
		else
		{
			escapedString += *i;
		}
	}
	return escapedString;
}

UTF8String SCRAM::UnescapeString(UTF8String str)
{
	UTF8String unescapedString;
	UTF8String::const_iterator i = str.begin();
	while(i + 2 != str.end())
	{
		if(*i == '=' && *(i + 1) == '3' && *(i + 2) == 'D')
		{
			unescapedString += "=";
			i += 3;
		}
		else if(*i == '=' && *(i + 1) == '2' && *(i + 2) == 'C')
		{
			unescapedString += ",";
			i += 3;
		}
		else
		{
			unescapedString += *i;
			i += 1;
		}
	}
	unescapedString += *i++;
	unescapedString += *i++;
	return unescapedString;
}

std::map<char, UTF8String> SCRAM::ParseSCRAMMessage(UTF8String message)
{
	std::map<char, UTF8String> parsedData;
	size_t pos = message.find(','), begin = 0;

	while(pos != UTF8String::npos)
	{
		parsedData.insert(ParseSCRAMField(message.substr(begin, pos - begin)));
		begin = pos + 1;
		pos = message.find(',', begin);
	}
	parsedData.insert(ParseSCRAMField(message.substr(begin)));

	return parsedData;
}

std::pair<char, UTF8String> SCRAM::ParseSCRAMField(UTF8String field)
{
	std::pair<char, UTF8String> invalidField(0, "");

	UTF8String::const_iterator i = field.begin();
	if(field.length() > 2)
	{
		char fieldName = *i;
		if(*++i != '=')
		{
			return invalidField;
		}
		UTF8String value = field.substr(2, field.length());
		return std::pair<char, UTF8String>(fieldName, UnescapeString(value));
	}
	else
	{
		return invalidField;
	}

}

void SCRAM::Error(std::wstring location,
				  std::wstring whenCalling,
				  unsigned errorCode)
{
	LPTSTR errorMessage;
	::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL, errorCode, 0, (LPTSTR)&errorMessage, 0, NULL);
	std::wostringstream dbgMsg;
	dbgMsg << L"SCRAM error in " << location <<
		L" when calling " << whenCalling <<
		std::hex <<	std::setw(8) <<	std::setfill(L'0') << 
		L": (ERROR 0x" << errorCode << L") " <<
		errorMessage <<	std::endl;
	OutputDebugString(dbgMsg.str().c_str());
	::LocalFree(errorMessage);
}