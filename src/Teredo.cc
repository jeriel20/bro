#include "Teredo.h"
#include "IP.h"
#include "Reporter.h"

void Teredo_Analyzer::Done()
	{
	Analyzer::Done();
	Event(udp_session_done);
	}

bool TeredoEncapsulation::DoParse(const u_char* data, int& len,
                                  bool found_origin, bool found_auth)
	{
	if ( len < 2 )
		{
		Weird("truncated_Teredo");
		return false;
		}

	uint16 tag = ntohs((*((const uint16*)data)));

	if ( tag == 0 )
		{
		// Origin Indication
		if ( found_origin )
			// can't have multiple origin indications
			return false;

		if ( len < 8 )
			{
			Weird("truncated_Teredo_origin_indication");
			return false;
			}

		origin_indication = data;
		len -= 8;
		data += 8;
		return DoParse(data, len, true, found_auth);
		}
	else if ( tag == 1 )
		{
		// Authentication
		if ( found_origin || found_auth )
			// can't have multiple authentication headers and can't come after
			// an origin indication
			return false;

		if ( len < 4 )
			{
			Weird("truncated_Teredo_authentication");
			return false;
			}

		uint8 id_len = data[2];
		uint8 au_len = data[3];
		uint16 tot_len = 4 + id_len + au_len + 8 + 1;

		if ( len < tot_len )
			{
			Weird("truncated_Teredo_authentication");
			return false;
			}

		auth = data;
		len -= tot_len;
		data += tot_len;
		return DoParse(data, len, found_origin, true);
		}
	else if ( ((tag & 0xf000)>>12) == 6 )
		{
		// IPv6
		if ( len < 40 )
			{
			Weird("truncated_IPv6_in_Teredo");
			return false;
			}

		if ( len - 40 != ntohs(((const struct ip6_hdr*)data)->ip6_plen) )
			{
			Weird("Teredo_payload_len_mismatch");
			return false;
			}

		inner_ip = data;
		return true;
		}

	return false;
	}

void Teredo_Analyzer::DeliverPacket(int len, const u_char* data, bool orig,
                                    int seq, const IP_Hdr* ip, int caplen)
	{
	Analyzer::DeliverPacket(len, data, orig, seq, ip, caplen);

	TeredoEncapsulation te(this);

	if ( ! te.Parse(data, len) )
		{
		ProtocolViolation("Bad Teredo encapsulation", (const char*) data, len);
		return;
		}

	const Encapsulation* e = Conn()->GetEncapsulation();

	if ( e && e->Depth() >= BifConst::Tunnel::max_depth )
		{
		Weird("tunnel_depth");
		return;
		}

	IP_Hdr* inner = 0;
	int rslt = sessions->ParseIPPacket(len, te.InnerIP(), IPPROTO_IPV6, inner);

	if ( rslt == 0 )
		ProtocolConfirmation();
	else if ( rslt < 0 )
		ProtocolViolation("Truncated Teredo", (const char*) data, len);
	else
		ProtocolViolation("Teredo payload length", (const char*) data, len);

	if ( rslt != 0 ) return;

	// TODO: raise Teredo-specific events for bubbles, origin/authentication

	Encapsulation* outer = new Encapsulation(e);
	EncapsulatingConn ec(Conn(), BifEnum::Tunnel::TEREDO);
	outer->Add(ec);

	sessions->DoNextInnerPacket(network_time, 0, inner, outer);

	delete inner;
	delete outer;
	}
