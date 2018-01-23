#include "host_data_receiver.h"

using namespace std;
//namespace py = pybind11;

//Constants
static const uint32_t SDP_PACKET_START_SENDING_COMMAND_ID = 100;
static const uint32_t SDP_PACKET_START_MISSING_SEQ_COMMAND_ID = 1000;
static const uint32_t SDP_PACKET_MISSING_SEQ_COMMAND_ID = 1001;
//static const int SDP_PACKET_PORT = 2;
static const uint32_t SDP_RETRANSMISSION_HEADER_SIZE = 10;
static const uint32_t SDP_PACKET_START_SENDING_COMMAND_MESSAGE_SIZE = 3;
static const uint32_t ACK_MESSAGE_CODE = 0;

// time out constants
static const int TIMEOUT_PER_RECEIVE_IN_SECONDS = 1;
static const int TIMEOUT_PER_SENDING_IN_MICROSECONDS = 10000;

// consts for data and converting between words and bytes
//static const int SDRAM_READING_SIZE_IN_BYTES_CONVERTER = 1024 * 1024;
static const int DATA_PER_FULL_PACKET = 68;
static const int DATA_PER_FULL_PACKET_WITH_SEQUENCE_NUM =
	DATA_PER_FULL_PACKET - 1;
static const int WORD_TO_BYTE_CONVERTER = 4;
static const int LENGTH_OF_DATA_SIZE = 4;
static const int END_FLAG_SIZE = 4;
static const int END_FLAG_SIZE_IN_BYTES = 4;
static const int SEQUENCE_NUMBER_SIZE = 4;
static const int END_FLAG = 0xFFFFFFFF;
static const int LAST_MESSAGE_FLAG_BIT_MASK = 0x80000000;
static const int TIMEOUT_RETRY_LIMIT = 1;

//vector<uint32_t> missing;

// Constructor
host_data_receiver::host_data_receiver(int port_connection, int placement_x, int placement_y, int placement_p,
		char *hostname, int length_in_bytes, int memory_address, int chip_x, int chip_y, int chip_p, int iptag, uint32_t window_size, uint32_t sliding_window) {

	this->port_connection = port_connection; 
	this->placement_x = placement_x; 
	this->placement_y = placement_y; 
	this->placement_p = placement_p;
	this->hostname = hostname;
	this->length_in_bytes = (uint32_t)length_in_bytes; 
	this->memory_address = (uint32_t)memory_address;
	this->chip_x = chip_x;
	this->chip_y = chip_y;
	this->chip_p = chip_p;
	this->iptag = iptag;

	//ack size
	this->window_size = window_size;
	//size of sliding window
	this->sliding_window = sliding_window;

	// allocate queue for messages
	this->messqueue = new PQueue<packet>();

	this->buffer = new char[length_in_bytes];

	this->max_seq_num = calculate_max_seq_num(length_in_bytes);
	this->last_seq = this->max_seq_num;

	this->rdr.thrown = false;
	this->pcr.thrown = false;

	this->finished_transfer = false;
	this->window_start = 0;
	this->window_end = this->window_size-1;
	this->is_last = false;
}

// Function for allocating an SCP Message
char * host_data_receiver::build_scp_req(uint16_t cmd, uint32_t port, int strip_sdp, uint32_t ip_address) {

	uint16_t seq = 0;
	uint32_t arg = 0;

	char *buffertmp = new char[4*sizeof(uint32_t)];

	memcpy(buffertmp, &cmd, sizeof(uint16_t));
	memcpy(buffertmp+sizeof(uint16_t), &seq, sizeof(uint16_t));

	arg = arg | (strip_sdp << 28) | (1 << 16) | this->iptag;
	memcpy(buffertmp+sizeof(uint32_t), &arg, sizeof(uint32_t));
	memcpy(buffertmp+2*sizeof(uint32_t), &port, sizeof(uint32_t));
	memcpy(buffertmp+3*sizeof(uint32_t), &ip_address, sizeof(uint32_t));


	return buffertmp;
}


//Function for asking data to the SpiNNaker system
void host_data_receiver::send_initial_command(UDPConnection *sender, UDPConnection *receiver) {

	//Build an SCP request to set up the IP Tag associated to this socket
	char *scp_req = build_scp_req((uint16_t)26, receiver->get_local_port(), 1, receiver->get_local_ip());

	SDPMessage ip_tag_message = SDPMessage(
		this->chip_x, this->chip_y, 0, 0, SDPMessage::REPLY_EXPECTED,
		255, 255, 255, 0, 0, scp_req, 4*sizeof(uint32_t));

	//Send SCP request
	sender->send_data(ip_tag_message.convert_to_byte_array(),
					 ip_tag_message.length_in_bytes());

	char buf[300];

	sender->receive_data(buf, 300);

    // Create Data request SDP packet
	char start_message_data[3*sizeof(uint32_t)];

    // add data
	memcpy(start_message_data, &SDP_PACKET_START_SENDING_COMMAND_ID, sizeof(uint32_t));
	memcpy(start_message_data+sizeof(uint32_t), &this->memory_address, sizeof(uint32_t));
	memcpy(start_message_data+2*sizeof(uint32_t), &this->length_in_bytes, sizeof(uint32_t));

    // build SDP message
    SDPMessage message = SDPMessage(
        this->placement_x, this->placement_y, this->placement_p, this->port_connection,
        SDPMessage::REPLY_NOT_EXPECTED, 255, 255, 255, 0, 0, start_message_data,
        3*sizeof(uint32_t));

    //send message
    sender->send_data(message.convert_to_byte_array(),
                     message.length_in_bytes());
}


//Function for computing expected maximum number of packets
uint32_t host_data_receiver::calculate_max_seq_num(uint32_t length) {

	int n_sequence_number;
	unsigned long data_left;
	float extra_n_sequences;

	n_sequence_number = 0;

	extra_n_sequences = (float)length / (float)(DATA_PER_FULL_PACKET_WITH_SEQUENCE_NUM * WORD_TO_BYTE_CONVERTER);

	//n_sequence_number += ceil(extra_n_sequences);
	//ERA QUELLO COMMENTATO PRIMA
	n_sequence_number += extra_n_sequences;

	return (uint32_t)n_sequence_number;
}


//Function for checking that all packets have been received
bool host_data_receiver::check(set<uint32_t> *received_seq_nums, uint32_t max_needed) {

	uint32_t recvsize = received_seq_nums->size();

	if(recvsize > (max_needed + 1)) {

		throw "Received more data than expected";
	}
	if(recvsize != (max_needed + 1)) {

		return false;
	}
	return true;
}

void host_data_receiver::send_ack(UDPConnection *sender, uint32_t window) {

	char data_field[2*sizeof(uint32_t)];

	//Used char array in order to modify it for selective ACK or for greater message
	memcpy(data_field, &ACK_MESSAGE_CODE, sizeof(uint32_t));
	memcpy(data_field+sizeof(uint32_t), &window, sizeof(uint32_t));

	cout << "SENDING ACK TO " << this->chip_x << this->chip_y << this->chip_p << endl;

	// build SDP message to be sent to the ETHERNET CHIP!
    SDPMessage message = SDPMessage(
        this->chip_x, this->chip_y, this->chip_p, 1,
        SDPMessage::REPLY_NOT_EXPECTED, 255, 255,
        255, 0, 0, data_field, 2*sizeof(uint32_t));

    //Send ACK
    sender->send_data(message.convert_to_byte_array(), message.length_in_bytes());
}


// Function for processing each received packet and checking end of transmission
void host_data_receiver::process_data(UDPConnection *sender, bool *finished, 
										set<uint32_t> *received_seq_nums, char *recvdata, int datalen,
										uint32_t *received_seqs, set<uint32_t> **received_in_windows) {

	int length_of_data, i, j;
	uint32_t last_mc_packet, first_packet_element, offset, true_data_length, seq_num;
	bool is_end_of_stream;

	cout << "IN PROCESS DATA" << endl;

	//Data size of the packet
	length_of_data = datalen;

	memcpy(&first_packet_element, recvdata, sizeof(uint32_t));

	seq_num = first_packet_element & 0x7FFFFFFF;

	cout << "RECEIVED " << seq_num << endl;

	//If received seq is lower than the window discard it as it has already been received
	//window check is performed in any case to be sure to shift the window
	if(seq_num >= window_start) {

		is_end_of_stream = ((first_packet_element & LAST_MESSAGE_FLAG_BIT_MASK) != 0) ? true : false;

		if(seq_num > this->max_seq_num) {

			throw "Got insane sequence number";
		}

		offset = (seq_num) * DATA_PER_FULL_PACKET_WITH_SEQUENCE_NUM * WORD_TO_BYTE_CONVERTER;

		true_data_length = (offset + length_of_data - SEQUENCE_NUMBER_SIZE);

		if(is_end_of_stream && length_of_data == END_FLAG_SIZE_IN_BYTES) {


		}
		else {

			memcpy(buffer+offset, recvdata+SEQUENCE_NUMBER_SIZE, (true_data_length-offset));
		}

		cout << "PROCESSING SEQUENCE NUMBER "  << seq_num << endl;

		received_seq_nums->insert(seq_num);

		received_in_windows[seq_num/this->window_size]->insert(seq_num);

		(*received_seqs)++;

		//Check for transmission termination
		if(is_end_of_stream) {

			cout << "END OF STREAM" << endl;

			cout << "WINDOW START " << this->window_start << " MAX SEQ NUM " << this->max_seq_num << endl;

			//Got end of stream but received less than max_seq_num seqs, check if something is missing
			if(!check(received_seq_nums, this->max_seq_num)) {

				//If this is last window
				if(this->window_end >= seq_num) {

					this->is_last = true;
					this->last_seq = seq_num;

					cout << "LAST WINDOW, NUMBER OF SEQUENCES HERE: " << (this->last_seq + 1) - this->window_start << endl;
					
					//If all sequences in last window have been received
					if(check(received_in_windows[this->window_start/this->window_size], ((seq_num + 1) - this->window_start) - 1)) {

						*finished = true;
						send_ack(sender, this->window_start/this->window_size);

						cout << "SENT LAST ACK" << endl;
						return;
					}
				}
			}
			else {

				*finished = true;
				send_ack(sender, this->window_start/this->window_size);

				cout << "SENT LAST ACK" << endl;
				return;
			}
		}

		//Check if it is possible to shift the window
		if(*received_seqs >= this->window_size) {

			cout << "CHECKING IF WINDOW " << seq_num/this->window_size << " IS COMPLETE" << endl;

			//If last window
			if(this->is_last) {

				cout << "LAST WINDOW, NUMBER OF SEQUENCES HERE: " << (this->last_seq + 1) - this->window_start << " RCVD IN SET " << received_in_windows[this->window_start/this->window_size]->size() << endl;

				//If all sequences in last window have been received
				if(check(received_in_windows[this->window_start/this->window_size], ((this->last_seq + 1) - this->window_start) - 1)) {

					*finished = true;
					send_ack(sender, this->window_start/this->window_size);

					cout << "SENT LAST ACK" << endl;
					return;
				}
			}
			else if(check(received_in_windows[this->window_start/this->window_size], this->window_size-1)) {

				send_ack(sender, this->window_start/this->window_size);

				cout << "WINDOW COMPLETE, ACK SENT" << endl;

				//Add check to not overcome max_seq_num boundary!!
				this->window_start += this->window_size;
				this->window_end += this->window_size;

				cout << "NEW WINDOW: " << this->window_start << " " << this->window_end << endl;
			}

		}
	}
	else {
		//In case ACK has not been received
		cout << "RESENDING ACK" << endl;
		send_ack(sender, seq_num/this->window_size);		
	}

	cout << "RETURNING FROM PROCESS DATA" << endl;

}

void host_data_receiver::reader_thread(UDPConnection *receiver) {

	char data[400];
	int recvd;
	packet p;

	// While socket is open add messages to the queue
	do {

		try {

			recvd = receiver->receive_data(data, 400);

			memcpy(p.content, data, recvd*sizeof(char));
			p.size = recvd;

		} catch(char const *e) {

			this->rdr.thrown = true;
			this->rdr.val = e;
			return;
		}

		if(recvd)
			messqueue->push(p);

		//If the other thread trew an exception(no need for mutex, in the worst case this thread will add an additional value to the queue)
		if(this->pcr.thrown == true)
			return;

	}while(recvd);
}

void host_data_receiver::processor_thread(UDPConnection *sender) {

	char data[400];
	int receivd = 0, timeoutcount = 0, datalen;
	bool finished = false;
	set<uint32_t> *received_seq_nums = new set<uint32_t>;
	set<uint32_t> **received_in_windows = new set<uint32_t> *[(int)ceil((float)this->max_seq_num/(float)(this->window_size))];
	packet p;
	uint32_t received_seqs = 0;

	for(int i = 0 ; i < (int)ceil((float)this->max_seq_num/(float)(this->window_size)) ; i++)
		received_in_windows[i] = new set<uint32_t>;

	cout << "STRUCT ALLOCATED" << endl;

	while(!finished) {

		try {

		 	p = messqueue->pop();

		 	memcpy(data, p.content, p.size*sizeof(char));
		 	datalen = p.size;


		 	process_data(sender, &finished, received_seq_nums, data, datalen, &received_seqs, received_in_windows);

		 }catch(TimeoutQueueException e) {

		 	//Eventually increase timeout to include the one added board side, here we aren't asking for retransmission anymore!
		 	if (timeoutcount > TIMEOUT_RETRY_LIMIT) {

				this->pcr.thrown = true;
				this->pcr.val = "Failed to hear from the machine. Please try removing firewalls";
				//Verify
				delete sender;
				return;

			}

		 	timeoutcount++;

		 }catch(const char *e) {

		 	this->pcr.thrown = true;
			this->pcr.val = e;
			delete sender;
			return;
		 }

		 if(this->rdr.thrown == true)
		 	return;
	}

	// close socket and inform the reader that transmission is completed
	delete sender;
	this->finished_transfer = true;
}

// Function externally callable for data gathering. It returns a buffer containing read data
char * host_data_receiver::get_data() {

	int datalen, timeoutcount;
	double seconds_taken;

	try {

		// create connection
		UDPConnection *sender =  new UDPConnection(NULL, NULL, 17893, this->hostname);

		// send the initial command to start data transmission
		send_initial_command(sender, sender);

		cout << "max_seq_num: " << this->max_seq_num << " window size: " << this->window_size << " sliding window " << this->sliding_window << endl;

		thread reader(&host_data_receiver::reader_thread, this, sender);
		thread processor(&host_data_receiver::processor_thread, this, sender);

		reader.join();
		processor.join();

		sleep(1);

		if(this->pcr.thrown == true) {

			cout << this->pcr.val << endl;
			return NULL;
		} 
		else if(this->rdr.thrown == true && this->finished_transfer == false) {

			cout << this->rdr.val << endl;
			return NULL;
		}


	}catch(char const *e) {

		cout << e << endl;

		return NULL;
	}

	return this->buffer;
}

/*
//Same behavior of get_data() function, but returns a valid type for python code
py::bytes host_data_receiver::get_data_for_python() {

	get_data();

	std::string *str = new string((const char *)this->buffer, this->length_in_bytes);

	return py::bytes(*str);
}*/


// Function externally callable for data gathering. It can be called by multiple threads simultaneously
void host_data_receiver::get_data_threadable(char *filepath_read, char *filepath_missing) {

	FILE *fp1, *fp2;

	get_data();

	cout << "WRITING ON FILE" << endl;

	fp1 = fopen(filepath_read, "wb");
	//fp2 = fopen(filepath_missing, "w");

	fwrite(this->buffer, sizeof(char), length_in_bytes, fp1);

	/*vector<uint32_t>::iterator i;
	char *miss = new char[sizeof(uint32_t) * missing.size()];
	int offset = 0;

	for(i = missing.begin() ; i != missing.end() ; i++) {

		uint32_t v = (uint32_t)*i;
		fprintf(fp2, "%u\n", v);
	}*/

	fclose(fp1);
	//fclose(fp2);

}
/*
//Python Binding

PYBIND11_MODULE(host_data_receiver, m) {

	m.doc() = "C++ data speed up packet gatherer machine vertex";

	py::class_<host_data_receiver>(m, "host_data_receiver")
		.def(py::init<>())
		.def("get_data_threadable", &host_data_receiver::get_data_threadable)
		.def("get_data", &host_data_receiver::get_data)
		.def("get_data_for_python", &host_data_receiver::get_data_for_python);
}
*/
