#include <Windows.h>
#include <stdio.h>

#pragma pack (1)
typedef struct {
	CHAR  magic[12];		/* "PackFile    " */
	DWORD entries;
} arc_header_t;

typedef struct
{
	char name[6 * 0x10];//������ȹ�����
	unsigned long offset;//���offset��һ�����ֵ
	unsigned long length;
	unsigned long reserved1;//λ�ã�Ҳ����δ���ķ�չ�л��¼˵�Ǹ����ܵ�����
	unsigned long reserved2;
	unsigned long reserved3;
	unsigned long reserved4;
	unsigned long reserved5;
	unsigned long reserved6;
} arc_entry_t;

typedef struct
{
	CHAR  magic[16];			/* "DSC FORMAT 1.00" */
	DWORD key;
	DWORD uncomprlen;
	DWORD dec_counts;			/* ���ܵĴ��� */
	DWORD reserved;
} dsc_header_t;

#pragma pop ()



struct dsc_huffman_code {
	WORD code;
	WORD depth;		/* ��Ҷ�ڵ����ڵ�depth��0��ʼ�� */
};

struct dsc_huffman_node {
	unsigned int is_parent;
	unsigned int code;	/* ����С��256�ģ�code�����ֽ����ݣ����ڴ���256�ģ����ֽڵ�1�Ǳ�־��lzѹ���������ֽ���copy_bytes��������Ҫ+2�� */
	unsigned int left_child_index;
	unsigned int right_child_index;
};

struct bits {
	unsigned long curbits;
	unsigned long curbyte;
	unsigned char cache;
	unsigned char *stream;
	unsigned long stream_length;
};

void bits_init(struct bits *bits, unsigned char *stream, unsigned long stream_length)
{
	memset(bits, 0, sizeof(*bits));
	bits->stream = stream;
	bits->stream_length = stream_length;
}

#if 1

#if 1
int bits_get_high(struct bits *bits, unsigned int req_bits, unsigned int *retval)
{
	unsigned int bits_value = 0;

	while (req_bits > 0) {
		unsigned int req;

		if (!bits->curbits) {
			if (bits->curbyte >= bits->stream_length)
				return -1;
			bits->cache = bits->stream[bits->curbyte++];
			bits->curbits = 8;
		}

		if (bits->curbits < req_bits)
			req = bits->curbits;
		else
			req = req_bits;

		bits_value <<= req;
		bits_value |= bits->cache >> (bits->curbits - req);
		bits->cache &= (1 << (bits->curbits - req)) - 1;
		req_bits -= req;
		bits->curbits -= req;
	}
	*retval = bits_value;
	return 0;
}
#else
static BYTE table0[8] = {
	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};
static BYTE table1[8] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};
int bits_get_high(struct bits *bits, unsigned int req_bits, unsigned int *retval)
{
	unsigned int bits_value = 0;
	for (int i = req_bits - 1; i >= 0; i--) {
		if (!bits->curbits) {
			if (bits->curbyte >= bits->stream_length)
				return -1;
			bits->cache = bits->stream[bits->curbyte++];
		}
		if (bits->cache & table0[bits->curbits++])
			bits_value |= table1[i & 7];
		bits->curbits &= 7;
		if (!(i & 7) && i)
			bits_value <<= 8;
	}
	*retval = bits_value;
	return 0;
}
#endif

int bit_get_high(struct bits *bits, void *retval)
{
	return bits_get_high(bits, 1, (unsigned int *)retval);
}

#else
/* �Ѿ������λ����ĵط���ֵ���ظ�retval */
int bit_get_high(struct bits *bits, void *retval)
{
	if (!bits->curbits) {
		if (bits->curbyte >= bits->stream_length)
			return -1;
		bits->cache = bits->stream[bits->curbyte++];
		bits->curbits = 8;
	}
	bits->curbits--;
	*(unsigned char *)retval = bits->cache >> bits->curbits;
	bits->cache &= (1 << bits->curbits) - 1;
	return 0;
}
/* FIXME��ʵ�ִ���Ӧ���ǰѾ������λ����ĵط���ֵ���η��ظ�retval�ĴӸߵ����ֽڣ� */
#if 1
int bits_get_high(struct bits *bits, unsigned int req_bits, unsigned int *retval)
{
	for (unsigned int i = 0; i < req_bits; i++) {
		unsigned char bitval;

		if (bit_get_high(bits, &bitval))
			return -1;

		*retval <<= 1;
		*retval |= bitval & 1;
	}
	*retval &= (1 << req_bits) - 1;

	return 0;
}
#else
int bits_get_high(struct bits *bits, unsigned int req_bits, unsigned int *retval)
{
	unsigned char byteval = 0;

	for (unsigned int i = 0; i < req_bits; i++) {
		unsigned char bitval;

		if (bit_get_high(bits, &bitval))
			return -1;

		byteval <<= 1;
		byteval |= bitval & 1;
		if (!((i + 1) & 7)) {
			((unsigned char *)retval)[i / 8] = byteval;
			byteval = 0;
		}
	}
	if (i & 7)
		((unsigned char *)retval)[(i - 1) / 8] = byteval;

	return 0;
}
#endif
#endif

/* ��setval�����λ���õ������λ����ĵط���ʼ */
int bit_put_high(struct bits *bits, unsigned char setval)
{
	bits->curbits++;
	bits->cache |= (setval & 1) << (8 - bits->curbits);
	if (bits->curbits == 8) {
		if (bits->curbyte >= bits->stream_length)
			return -1;
		bits->stream[bits->curbyte++] = bits->cache;
		bits->curbits = 0;
		bits->cache = 0;
	}
	return 0;
}

/* ���մӸ��ֽڵ����ֽڵ�˳���setval�е�ֵ���õ������λ����ĵط���ʼ */
int bits_put_high(struct bits *bits, unsigned int req_bits, void *setval)
{
	unsigned int this_bits;
	unsigned int this_byte;
	unsigned int i;

	this_byte = req_bits / 8;
	this_bits = req_bits & 7;
	for (int k = (int)this_bits - 1; k >= 0; k--) {
		unsigned char bitval;

		bitval = !!(((unsigned char *)setval)[this_byte] & (1 << k));
		if (bit_put_high(bits, bitval))
			return -1;
	}
	this_bits = req_bits & ~7;
	this_byte--;
	for (i = 0; i < this_bits; i++) {
		unsigned char bitval;

		bitval = !!(((unsigned char *)setval)[this_byte - i / 8] & (1 << (7 - (i & 7))));
		if (bit_put_high(bits, bitval))
			return -1;
	}

	return 0;
}

void bits_flush(struct bits *bits)
{
	bits->stream[bits->curbyte] = bits->cache;
}

#if 0
int bit_get_low(struct bits *bits, void *retval)
{
	int this_bits;
	unsigned char ret = 0;
	if (!bits->curbits) {
		if (bits->curbyte >= bits->stream_length)
			return -1;
		bits->cache = bits->stream[bits->curbyte++];
		bits->curbits = 8;
	}
	this_bits = req_bits <= bits->curbits ? req_bits : bits->curbits;
	bits->curbits -= this_bits;
	((unsigned char *)retval)[getbits / 8] = (bits->cache & ((1 << this_bits) - 1)) << (getbits & 7);
	bits->cache >>= this_bits;
	req_bits -= this_bits;
	getbits += this_bits;
	return 0;
}
#endif


static BYTE update_key(DWORD *key, BYTE *magic)
{
	DWORD v0, v1;

	v0 = (*key & 0xffff) * 20021;
	v1 = (magic[1] << 24) | (magic[0] << 16) | (*key >> 16);
	v1 = v1 * 20021 + *key * 346;
	v1 = (v1 + (v0 >> 16)) & 0xffff;
	*key = (v1 << 16) + (v0 & 0xffff) + 1;
	return v1 & 0x7fff;
}

/* ���������� */
static int dsc_huffman_code_compare(const void *code1, const void *code2)
{
	struct dsc_huffman_code *codes[2] = { (struct dsc_huffman_code *)code1, (struct dsc_huffman_code *)code2 };
	int compare = (int)codes[0]->depth - (int)codes[1]->depth;

	if (!compare)
		return (int)codes[0]->code - (int)codes[1]->code;

	return compare;
}

static void dsc_create_huffman_tree(struct dsc_huffman_node *huffman_nodes,
struct dsc_huffman_code *huffman_code,
	unsigned int leaf_node_counts)
{
	unsigned int nodes_index[2][512];	/* ���쵱ǰdepth�½ڵ������ĸ��������飬
										* �䳤���ǵ�ǰdepth�¿��ܵ����ڵ��� */
	unsigned int next_node_index = 1;	/* 0�Ѿ�Ĭ�Ϸ�������ڵ��� */
	unsigned int depth_nodes = 1;		/* ��0��ȵĽڵ���(2^N)Ϊ1 */
	unsigned int depth = 0;				/* ��ǰ����� */
	unsigned int switch_flag = 0;

	nodes_index[0][0] = 0;
	for (unsigned int n = 0; n < leaf_node_counts;) {
		unsigned int depth_existed_nodes;/* ��ǰdepth�´�����Ҷ�ڵ���� */
		unsigned int depth_nodes_to_create;	/* ��ǰdepth����Ҫ�����Ľڵ�����������һ����˵�൱�ڸ��ڵ㣩 */
		unsigned int *huffman_nodes_index;
		unsigned int *child_index;

		huffman_nodes_index = nodes_index[switch_flag];
		switch_flag ^= 1;
		child_index = nodes_index[switch_flag];

		depth_existed_nodes = 0;
		/* ��ʼ��������ͬһdepth�����нڵ� */
		while (huffman_code[n].depth == depth) {
			struct dsc_huffman_node *node = &huffman_nodes[huffman_nodes_index[depth_existed_nodes++]];
			/* Ҷ�ڵ㼯������� */
			node->is_parent = 0;
			node->code = huffman_code[n++].code;
		}
		depth_nodes_to_create = depth_nodes - depth_existed_nodes;
		for (unsigned int i = 0; i < depth_nodes_to_create; i++) {
			struct dsc_huffman_node *node = &huffman_nodes[huffman_nodes_index[depth_existed_nodes + i]];

			node->is_parent = 1;
			child_index[i * 2 + 0] = node->left_child_index = next_node_index++;
			child_index[i * 2 + 1] = node->right_child_index = next_node_index++;
		}
		depth++;
		depth_nodes = depth_nodes_to_create * 2;	/* ��һdepth���ܵĽڵ��� */
	}
}


unsigned int  dsc_huffman_decompress(struct dsc_huffman_node *huffman_nodes,
	unsigned int dec_counts, unsigned char *uncompr,
	unsigned int uncomprlen, unsigned char *compr,
	unsigned int comprlen)
{
	struct bits bits;
	unsigned int act_uncomprlen = 0;
	int err = 0;

	bits_init(&bits, compr, comprlen);
	for (unsigned int k = 0; k < dec_counts; k++) {
		unsigned char child;
		unsigned int node_index;
		unsigned int code;

		node_index = 0;
		do {
			if (bit_get_high(&bits, &child))
				goto out;

			if (!child)
				node_index = huffman_nodes[node_index].left_child_index;
			else
				node_index = huffman_nodes[node_index].right_child_index;
		} while (huffman_nodes[node_index].is_parent);

		code = huffman_nodes[node_index].code;

		if (code >= 256) {	// 12λ�����win_pos, lzѹ��
			unsigned int copy_bytes, win_pos;

			copy_bytes = (code & 0xff) + 2;
			if (bits_get_high(&bits, 12, &win_pos))
				break;

			win_pos += 2;
			for (unsigned int i = 0; i < copy_bytes; i++) {
				uncompr[act_uncomprlen] = uncompr[act_uncomprlen - win_pos];
				act_uncomprlen++;
			}
		}
		else
			uncompr[act_uncomprlen++] = code;
	}
out:
	return act_uncomprlen;
}


unsigned int dsc_decompress(dsc_header_t *dsc_header, unsigned int dsc_len,
	unsigned char *uncompr, unsigned int uncomprlen)
{
	/* Ҷ�ڵ���룺ǰ256����ͨ�ĵ��ֽ�����code����256��lzѹ����code��ÿ��code��Ӧһ�ֲ�ͬ��copy_bytes */
	struct dsc_huffman_code huffman_code[512];
	/* Ǳ�ڵģ��൱��˫�ֽڱ��루���ֽ�ascii��˫�ֽ�lz�� */
	struct dsc_huffman_node huffman_nodes[1023];
	BYTE magic[2];
	DWORD key;
	unsigned char *enc_buf;

	enc_buf = (unsigned char *)(dsc_header + 1);
	magic[0] = dsc_header->magic[0];
	magic[1] = dsc_header->magic[1];
	key = dsc_header->key;

	/* ����Ҷ�ڵ������Ϣ */
	memset(huffman_code, 0, sizeof(huffman_code));
	unsigned int leaf_node_counts = 0;
	for (unsigned int i = 0; i < 512; i++) {
		BYTE depth;

		depth = enc_buf[i] - update_key(&key, magic);
		if (depth) {
			huffman_code[leaf_node_counts].depth = depth;
			huffman_code[leaf_node_counts].code = i;
			leaf_node_counts++;
		}
	}

	qsort(huffman_code, leaf_node_counts, sizeof(struct dsc_huffman_code), dsc_huffman_code_compare);

	dsc_create_huffman_tree(huffman_nodes, huffman_code, leaf_node_counts);

	unsigned char *compr = enc_buf + 512;
	unsigned int act_uncomprlen;
	act_uncomprlen = dsc_huffman_decompress(huffman_nodes, dsc_header->dec_counts,
		uncompr, dsc_header->uncomprlen, compr, dsc_len - sizeof(dsc_header_t) - 512);

	return act_uncomprlen;
}

int wmain(int argc, WCHAR* argv[])
{
	if (argc != 2)
		return 0;

	FILE* fin = _wfopen(argv[1], L"rb");
	arc_header_t Header;
	fread(&Header, 1, sizeof(arc_header_t), fin);
	arc_entry_t* Chunk = new arc_entry_t[Header.entries];
	fread(Chunk, 1, Header.entries * sizeof(arc_entry_t), fin);

	DWORD PostOffset = sizeof(arc_header_t) + Header.entries * sizeof(arc_entry_t);

	for (DWORD i = 0; i < Header.entries; i++)
	{
		fseek(fin, PostOffset + Chunk[i].offset, SEEK_SET);

		FILE* fout = fopen(Chunk[i].name, "wb");
		PBYTE Buffer = new BYTE[Chunk[i].length];
		fread(Buffer, 1, Chunk[i].length, fin);

		dsc_header_t* dsc_header = (dsc_header_t*)Buffer;

		DWORD WriteSize = Chunk[i].length;

		if (!memcmp(dsc_header->magic, "DSC FORMAT 1.00", 16))
		{
			PBYTE uncompr = new BYTE[dsc_header->uncomprlen];

			DWORD act_uncomprlen = dsc_decompress(dsc_header, Chunk[i].length,
				uncompr, dsc_header->uncomprlen);
			if (act_uncomprlen != dsc_header->uncomprlen)
			{
				delete[] uncompr;
			}
			delete[] Buffer;
			Buffer = uncompr;
			WriteSize = act_uncomprlen;
		}

		fwrite(Buffer, 1, WriteSize, fout);
		fclose(fout);
		delete[] Buffer;
	}
	fclose(fin);
	delete[] Chunk;

	return 0;
}

