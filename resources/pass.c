bool IpModem::SendPassword( const char *password, uint32_t seed )
64 {
65         if( !password || strlen(password) == 0  ) {
66                 throw BadPassword("Logic error: No password provided in SendPassword.", 0, false);
67         }
68
69         int read_ep  = m_con.GetProbeResult().m_epModem.read;
70         int write_ep = m_con.GetProbeResult().m_epModem.write;
71         unsigned char pwdigest[SHA_DIGEST_LENGTH];
72         unsigned char prefixedhash[SHA_DIGEST_LENGTH + 4];
73         unsigned char pw_response[SHA_DIGEST_LENGTH + 8];
74         uint32_t new_seed;
75         Data data;
76
77         if( !password || strlen(password) == 0  ) {
78                 throw BadPassword("No password provided.", 0, false);
79         }
80
81         // Build the password hash
82         // first, hash the password by itself
83         SHA1((unsigned char *) password, strlen(password), pwdigest);
84
85         // prefix the resulting hash with the provided seed
86         memcpy(&prefixedhash[0], &seed, sizeof(uint32_t));
87         memcpy(&prefixedhash[4], pwdigest, SHA_DIGEST_LENGTH);
88
89         // hash again
90         SHA1((unsigned char *) prefixedhash, SHA_DIGEST_LENGTH + 4, pwdigest);
91
92         // Build the response packet
93         const char pw_rsphdr[]  = { 0x03, 0x00, 0x00, 0x00 };
94         memcpy(&pw_response[0], pw_rsphdr, sizeof(pw_rsphdr));
95         memcpy(&pw_response[4], pwdigest, SHA_DIGEST_LENGTH);
96         memcpy(&pw_response[24], special_flag, sizeof(special_flag));
97
98         // Send the password response packet
99         m_dev.BulkWrite(write_ep, pw_response, sizeof(pw_response));
100         m_dev.BulkRead(read_ep, data);
101         ddout("IPModem: Read password response.\n" << data);
102
103         // Added for the BB Storm 9000's second password request
104         if( data.GetSize() >= 16 && data.GetData()[0] == 0x00 ) {
105                 try {
106                         m_dev.BulkRead(read_ep, data, 500);
107                         ddout("IPModem: Null Response Packet:\n" << data);
108                 }
109                 catch( Usb::Timeout &to ) {
110                         // do nothing on timeouts
111                         ddout("IPModem: Null Response Timeout");
112                 }
113         }
114
115         //
116         // check response 04 00 00 00 .......
117         // On the 8703e the seed is incremented, retries are reset to 10
118         // when the password is accepted.
119         //
120         // If data.GetData() + 4 is = to the orginal seed +1 or 00 00 00 00
121         // then the password was acceppted.
122         //
123         // When data.GetData() + 4 is not 00 00 00 00 then data.GetData()[8]
124         // contains the number of password retrys left.
125         //
126         if( data.GetSize() >= 9 && data.GetData()[0] == 0x04 ) {
127                 memcpy(&new_seed, data.GetData() + 4, sizeof(uint32_t));
128                 seed++;
129                 if( seed == new_seed || new_seed == 0 ) {
130                         ddout("IPModem: Password accepted.\n");
131
132 #if SHA_DIGEST_LENGTH < SB_IPMODEM_SESSION_KEY_LENGTH
133 #error Session key field must be smaller than SHA digest
134 #endif
135                         // Create session key - last 8 bytes of the password hash
136                         memcpy(&m_session_key[0],
137                                 pwdigest + SHA_DIGEST_LENGTH - sizeof(m_session_key),
138                                 sizeof(m_session_key));
139
140                         // blank password hashes as we don't need these anymore
141                         memset(pwdigest, 0, sizeof(pwdigest));
142                         memset(prefixedhash, 0, sizeof(prefixedhash));
143                         return true;
144                 }
145                 else {
146                         ddout("IPModem: Invalid password.\n" << data);
147                         throw BadPassword("Password rejected by device.", data.GetData()[8], false);
148                 }
149         }
150         // Unknown packet
151         ddout("IPModem: Error unknown packet.\n" << data);
152         return false;
153 }