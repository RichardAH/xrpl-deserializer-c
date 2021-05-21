/**
 * XRPL Deserializer 
 * Author: Richard Holland
 * Date: 21/5/21
 * Pass a hex encoded xrpl binary object via argument to the executable
 * Output: JSON
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libbase58.h"

#include "sha-256.h"

#define DEFAULT_SIZE (2048*1024)

int append(int indent_level, uint8_t** output, int* upto, int* len, uint8_t* append, int append_len)
{

    //printf("append: `%s`\n", append);
    if (*len - *upto < append_len + 1 + indent_level)
    {
        *len *= 2;
        *output = realloc(*output, *len + 1);
        if (*output == 0)
            return 0;
    }

    // tabs for indent

    for (int i = 0; i < indent_level; ++i)
        *(*output + (*upto)++) = '\t';

    for (uint8_t* x = *output + *upto, *end = *output + *upto + append_len; x < end && *append; (*upto)++)
        *x++ = *append++;

    *(*output + *upto) = '\0';

    return 1;
}

#define SBUF(x) x,sizeof(x)
#define APPENDPARAMS indent_level, output, &upto, &len
#define APPENDNOINDENT 0, output, &upto, &len

#define REQUIRE(b)\
{\
    if (remaining < (b))\
    {\
        fprintf(stderr, "Error: expecting %d bytes at %d but input was short\n", (b), upto);\
        return 0;\
    }\
}

#define ADVANCE(x)\
{\
    n += x; remaining -= x;\
}

#define HEX(out_raw, in_raw, len_raw)\
{\
    uint8_t* out = (out_raw);\
    uint8_t* in = (in_raw);\
    uint64_t len = (len_raw);\
    for (int i = 0; i < len; ++i)\
    {\
        unsigned char hi = in[i] >> 4U;\
        unsigned char lo = in[i] & 0xFU;\
        hi += (hi > 9 ? 'A' - 10 : '0');\
        lo += (lo > 9 ? 'A' - 10 : '0');\
        out[i*2+0] = (char)hi;\
        out[i*2+1] = (char)lo;\
    }\
}


int deserialize(uint8_t** output, uint8_t* input, int input_len)
{

    
    int len = DEFAULT_SIZE;
    *output = malloc(len);
    int upto = 0;

    int remaining = input_len - 1;
    uint8_t* n = input;

    int object_level = 0;
    int array_level = 0;

    int indent_level = 0;
    append(APPENDPARAMS, SBUF("{\n"));

    indent_level++;
    int nocomma = 0;
    while (remaining >= 0)
    {

        if (array_level < 0)
        {
            fprintf(stderr, "More close arrays than open arrays! at %d\n", upto);
            return 0;
        }
        if (object_level < 0)
        {
            fprintf(stderr, "More close objects than open objects! at %d\n", upto);
            return 0;
        }

        if (n != input && !nocomma)
            append(APPENDNOINDENT, SBUF(",\n"));

        nocomma = 0;

        int field_code = -1;
        int type_code = -1;

        if (n == 0)
        {
            // 3 byte header
            if (remaining < 2)
            {
                fprintf(stderr, "\nError parsing 3 byte header, not enough bytes remaining\n");
                return 0;
            }

            type_code = *(n+1);
            field_code = *(n+2);
            n += 3; remaining -= 3;

        }
        else if ((*n >> 4U) == 0)
        {
            // 2 byte header (typecode >= 16 && field code < 16)
            if (remaining < 1)
            {
                fprintf(stderr, "\nError parsing 2 byte header, not enough bytes remaining\n");
                return 0;
            }
            field_code = (*n & 0xFU);
            type_code = *(n+1);
            n += 2; remaining -= 2;
        }
        else if ((*n & 0xFU) == 0)
        {
            // 2 byte header (typecode < 16 && field code >= 16)
            if (remaining < 1)
            {
                fprintf(stderr, "\nError parsing 2 byte header, not enough bytes remaining\n");
                return 0;
            }
            type_code = (*n >> 4U);
            field_code = *(n+1);
            n += 2; remaining -= 2;
        }
        else
        {
            // 1 byte header

            type_code = (*n >> 4U);
            field_code = (*n & 0xFU);
            n++; remaining--;
        }

        if (type_code == 0)
        {
            fprintf(stderr, "Invalid typecode 0 at %d\n", upto);

            return 0;
        }

        int error = 0;

        int size = 
            ( type_code == 1 ? 2U : // UINT16
            ( type_code == 2 ? 4U : // uint32
            ( type_code == 3 ? 8U : // uint64
            ( type_code == 4 ? 16U : // uint128
            ( type_code == 5 ? 32U : // uint256
            ( type_code == 6 ? 8U  : // amount (8 bytes or 48 bytes)
            ( type_code == 7 ? 0U  : // blob vl
            ( type_code == 8 ? 21U : // account
            ( type_code == 16 ? 1U : // uint8
            ( type_code == 17 ? 20U : // uint160
            ( type_code == 18 ? 0U  : // pathset
            ( type_code == 19 ? 0U : // vector256
            ( type_code == 14 ? 0U : // array/object
            ( type_code == 15 ? 0U : // array/object
                (error=1)))))))))))))));
        
        if (error)
        {
            fprintf(stderr, "Error, unknown typecode %lu at byte %d\n", type_code, (input - n));
            return 0;
        }


        uint32_t field_id = (type_code << 16U) + field_code;

//        printf("field_id: %llx\n", field_id);

        if (field_id == -1UL) append(APPENDPARAMS, SBUF("Invalid: "));
        else if (field_id == 0UL) append(APPENDPARAMS, SBUF("Generic: "));
        else if (field_id == 0x27120101UL) append(APPENDPARAMS, SBUF("LedgerEntry: "));
        else if (field_id == 0x27110101UL) append(APPENDPARAMS, SBUF("Transaction: "));
        else if (field_id == 0x27130101UL) append(APPENDPARAMS, SBUF("Validation: "));
        else if (field_id == 0x27140101UL) append(APPENDPARAMS, SBUF("Metadata: "));
        else if (field_id == 0x50101UL) append(APPENDPARAMS, SBUF("Hash: "));
        else if (field_id == 0x50102UL) append(APPENDPARAMS, SBUF("Index: "));
        else if (field_id == 0x100001UL) append(APPENDPARAMS, SBUF("CloseResolution: "));
        else if (field_id == 0x100002UL) append(APPENDPARAMS, SBUF("Method: "));
        else if (field_id == 0x100003UL) append(APPENDPARAMS, SBUF("TransactionResult: "));
        else if (field_id == 0x100010UL) append(APPENDPARAMS, SBUF("TickSize: "));
        else if (field_id == 0x100011UL) append(APPENDPARAMS, SBUF("UNLModifyDisabling: "));
        else if (field_id == 0x10001UL) append(APPENDPARAMS, SBUF("LedgerEntryType: "));
        else if (field_id == 0x10002UL) append(APPENDPARAMS, SBUF("TransactionType: "));
        else if (field_id == 0x10003UL) append(APPENDPARAMS, SBUF("SignerWeight: "));
        else if (field_id == 0x10010UL) append(APPENDPARAMS, SBUF("Version: "));
        else if (field_id == 0x20002UL) append(APPENDPARAMS, SBUF("Flags: "));
        else if (field_id == 0x20003UL) append(APPENDPARAMS, SBUF("SourceTag: "));
        else if (field_id == 0x20004UL) append(APPENDPARAMS, SBUF("Sequence: "));
        else if (field_id == 0x20005UL) append(APPENDPARAMS, SBUF("PreviousTxnLgrSeq: "));
        else if (field_id == 0x20006UL) append(APPENDPARAMS, SBUF("LedgerSequence: "));
        else if (field_id == 0x20007UL) append(APPENDPARAMS, SBUF("CloseTime: "));
        else if (field_id == 0x20008UL) append(APPENDPARAMS, SBUF("ParentCloseTime: "));
        else if (field_id == 0x20009UL) append(APPENDPARAMS, SBUF("SigningTime: "));
        else if (field_id == 0x2000aUL) append(APPENDPARAMS, SBUF("Expiration: "));
        else if (field_id == 0x2000bUL) append(APPENDPARAMS, SBUF("erRate: "));
        else if (field_id == 0x2000cUL) append(APPENDPARAMS, SBUF("WalletSize: "));
        else if (field_id == 0x2000dUL) append(APPENDPARAMS, SBUF("OwnerCount: "));
        else if (field_id == 0x2000eUL) append(APPENDPARAMS, SBUF("DestinationTag: "));
        else if (field_id == 0x20010UL) append(APPENDPARAMS, SBUF("HighQualityIn: "));
        else if (field_id == 0x20011UL) append(APPENDPARAMS, SBUF("HighQualityOut: "));
        else if (field_id == 0x20012UL) append(APPENDPARAMS, SBUF("LowQualityIn: "));
        else if (field_id == 0x20013UL) append(APPENDPARAMS, SBUF("LowQualityOut: "));
        else if (field_id == 0x20014UL) append(APPENDPARAMS, SBUF("QualityIn: "));
        else if (field_id == 0x20015UL) append(APPENDPARAMS, SBUF("QualityOut: "));
        else if (field_id == 0x20016UL) append(APPENDPARAMS, SBUF("StampEscrow: "));
        else if (field_id == 0x20017UL) append(APPENDPARAMS, SBUF("BondAmount: "));
        else if (field_id == 0x20018UL) append(APPENDPARAMS, SBUF("LoadFee: "));
        else if (field_id == 0x20019UL) append(APPENDPARAMS, SBUF("OfferSequence: "));
        else if (field_id == 0x2001aUL) append(APPENDPARAMS, SBUF("FirstLedgerSequence: "));
        else if (field_id == 0x2001bUL) append(APPENDPARAMS, SBUF("LastLedgerSequence: "));
        else if (field_id == 0x2001cUL) append(APPENDPARAMS, SBUF("TransactionIndex: "));
        else if (field_id == 0x2001dUL) append(APPENDPARAMS, SBUF("OperationLimit: "));
        else if (field_id == 0x2001eUL) append(APPENDPARAMS, SBUF("ReferenceFeeUnits: "));
        else if (field_id == 0x2001fUL) append(APPENDPARAMS, SBUF("ReserveBase: "));
        else if (field_id == 0x20020UL) append(APPENDPARAMS, SBUF("ReserveIncrement: "));
        else if (field_id == 0x20021UL) append(APPENDPARAMS, SBUF("SetFlag: "));
        else if (field_id == 0x20022UL) append(APPENDPARAMS, SBUF("ClearFlag: "));
        else if (field_id == 0x20023UL) append(APPENDPARAMS, SBUF("SignerQuorum: "));
        else if (field_id == 0x20024UL) append(APPENDPARAMS, SBUF("CancelAfter: "));
        else if (field_id == 0x20025UL) append(APPENDPARAMS, SBUF("FinishAfter: "));
        else if (field_id == 0x20026UL) append(APPENDPARAMS, SBUF("SignerListID: "));
        else if (field_id == 0x20027UL) append(APPENDPARAMS, SBUF("SettleDelay: "));
        else if (field_id == 0x20028UL) append(APPENDPARAMS, SBUF("HookStateCount: "));
        else if (field_id == 0x20029UL) append(APPENDPARAMS, SBUF("HookReserveCount: "));
        else if (field_id == 0x2002aUL) append(APPENDPARAMS, SBUF("HookDataMaxSize: "));
        else if (field_id == 0x2002bUL) append(APPENDPARAMS, SBUF("EmitGeneration: "));
        else if (field_id == 0x30001UL) append(APPENDPARAMS, SBUF("IndexNext: "));
        else if (field_id == 0x30002UL) append(APPENDPARAMS, SBUF("IndexPrevious: "));
        else if (field_id == 0x30003UL) append(APPENDPARAMS, SBUF("BookNode: "));
        else if (field_id == 0x30004UL) append(APPENDPARAMS, SBUF("OwnerNode: "));
        else if (field_id == 0x30005UL) append(APPENDPARAMS, SBUF("BaseFee: "));
        else if (field_id == 0x30006UL) append(APPENDPARAMS, SBUF("ExchangeRate: "));
        else if (field_id == 0x30007UL) append(APPENDPARAMS, SBUF("LowNode: "));
        else if (field_id == 0x30008UL) append(APPENDPARAMS, SBUF("HighNode: "));
        else if (field_id == 0x30009UL) append(APPENDPARAMS, SBUF("DestinationNode: "));
        else if (field_id == 0x3000aUL) append(APPENDPARAMS, SBUF("Cookie: "));
        else if (field_id == 0x3000bUL) append(APPENDPARAMS, SBUF("ServerVersion: "));
        else if (field_id == 0x3000cUL) append(APPENDPARAMS, SBUF("EmitBurden: "));
        else if (field_id == 0x30010UL) append(APPENDPARAMS, SBUF("HookOn: "));
        else if (field_id == 0x40001UL) append(APPENDPARAMS, SBUF("EmailHash: "));
        else if (field_id == 0x110001UL) append(APPENDPARAMS, SBUF("TakerPaysCurrency: "));
        else if (field_id == 0x110002UL) append(APPENDPARAMS, SBUF("TakerPaysIssuer: "));
        else if (field_id == 0x110003UL) append(APPENDPARAMS, SBUF("TakerGetsCurrency: "));
        else if (field_id == 0x110004UL) append(APPENDPARAMS, SBUF("TakerGetsIssuer: "));
        else if (field_id == 0x50001UL) append(APPENDPARAMS, SBUF("LedgerHash: "));
        else if (field_id == 0x50002UL) append(APPENDPARAMS, SBUF("ParentHash: "));
        else if (field_id == 0x50003UL) append(APPENDPARAMS, SBUF("TransactionHash: "));
        else if (field_id == 0x50004UL) append(APPENDPARAMS, SBUF("AccountHash: "));
        else if (field_id == 0x50005UL) append(APPENDPARAMS, SBUF("PreviousTxnID: "));
        else if (field_id == 0x50006UL) append(APPENDPARAMS, SBUF("LedgerIndex: "));
        else if (field_id == 0x50007UL) append(APPENDPARAMS, SBUF("WalletLocator: "));
        else if (field_id == 0x50008UL) append(APPENDPARAMS, SBUF("RootIndex: "));
        else if (field_id == 0x50009UL) append(APPENDPARAMS, SBUF("AccountTxnID: "));
        else if (field_id == 0x5000aUL) append(APPENDPARAMS, SBUF("EmitParentTxnID: "));
        else if (field_id == 0x5000bUL) append(APPENDPARAMS, SBUF("EmitNonce: "));
        else if (field_id == 0x50010UL) append(APPENDPARAMS, SBUF("BookDirectory: "));
        else if (field_id == 0x50011UL) append(APPENDPARAMS, SBUF("InvoiceID: "));
        else if (field_id == 0x50012UL) append(APPENDPARAMS, SBUF("Nickname: "));
        else if (field_id == 0x50013UL) append(APPENDPARAMS, SBUF("Amendment: "));
        else if (field_id == 0x50014UL) append(APPENDPARAMS, SBUF("TicketID: "));
        else if (field_id == 0x50015UL) append(APPENDPARAMS, SBUF("Digest: "));
        else if (field_id == 0x50016UL) append(APPENDPARAMS, SBUF("PayChannel: "));
        else if (field_id == 0x50017UL) append(APPENDPARAMS, SBUF("ConsensusHash: "));
        else if (field_id == 0x50018UL) append(APPENDPARAMS, SBUF("CheckID: "));
        else if (field_id == 0x50019UL) append(APPENDPARAMS, SBUF("ValidatedHash: "));
        else if (field_id == 0x60001UL) append(APPENDPARAMS, SBUF("Amount: "));
        else if (field_id == 0x60002UL) append(APPENDPARAMS, SBUF("Balance: "));
        else if (field_id == 0x60003UL) append(APPENDPARAMS, SBUF("LimitAmount: "));
        else if (field_id == 0x60004UL) append(APPENDPARAMS, SBUF("TakerPays: "));
        else if (field_id == 0x60005UL) append(APPENDPARAMS, SBUF("TakerGets: "));
        else if (field_id == 0x60006UL) append(APPENDPARAMS, SBUF("LowLimit: "));
        else if (field_id == 0x60007UL) append(APPENDPARAMS, SBUF("HighLimit: "));
        else if (field_id == 0x60008UL) append(APPENDPARAMS, SBUF("Fee: "));
        else if (field_id == 0x60009UL) append(APPENDPARAMS, SBUF("SendMax: "));
        else if (field_id == 0x6000aUL) append(APPENDPARAMS, SBUF("DeliverMin: "));
        else if (field_id == 0x60010UL) append(APPENDPARAMS, SBUF("MinimumOffer: "));
        else if (field_id == 0x60011UL) append(APPENDPARAMS, SBUF("RippleEscrow: "));
        else if (field_id == 0x60012UL) append(APPENDPARAMS, SBUF("DeliveredAmount: "));
        else if (field_id == 0x70001UL) append(APPENDPARAMS, SBUF("PublicKey: "));
        else if (field_id == 0x70002UL) append(APPENDPARAMS, SBUF("MessageKey: "));
        else if (field_id == 0x70003UL) append(APPENDPARAMS, SBUF("SigningPubKey: "));
        else if (field_id == 0x70004UL) append(APPENDPARAMS, SBUF("TxnSignature: "));
        else if (field_id == 0x70006UL) append(APPENDPARAMS, SBUF("Signature: "));
        else if (field_id == 0x70007UL) append(APPENDPARAMS, SBUF("Domain: "));
        else if (field_id == 0x70008UL) append(APPENDPARAMS, SBUF("FundCode: "));
        else if (field_id == 0x70009UL) append(APPENDPARAMS, SBUF("RemoveCode: "));
        else if (field_id == 0x7000aUL) append(APPENDPARAMS, SBUF("ExpireCode: "));
        else if (field_id == 0x7000bUL) append(APPENDPARAMS, SBUF("CreateCode: "));
        else if (field_id == 0x7000cUL) append(APPENDPARAMS, SBUF("MemoType: "));
        else if (field_id == 0x7000dUL) append(APPENDPARAMS, SBUF("MemoData: "));
        else if (field_id == 0x7000eUL) append(APPENDPARAMS, SBUF("MemoFormat: "));
        else if (field_id == 0x70010UL) append(APPENDPARAMS, SBUF("Fulfillment: "));
        else if (field_id == 0x70011UL) append(APPENDPARAMS, SBUF("Condition: "));
        else if (field_id == 0x70012UL) append(APPENDPARAMS, SBUF("MasterSignature: "));
        else if (field_id == 0x70013UL) append(APPENDPARAMS, SBUF("UNLModifyValidator: "));
        else if (field_id == 0x70014UL) append(APPENDPARAMS, SBUF("NegativeUNLToDisable: "));
        else if (field_id == 0x70015UL) append(APPENDPARAMS, SBUF("NegativeUNLToReEnable: "));
        else if (field_id == 0x70016UL) append(APPENDPARAMS, SBUF("HookData: "));
        else if (field_id == 0x80001UL) append(APPENDPARAMS, SBUF("Account: "));
        else if (field_id == 0x80002UL) append(APPENDPARAMS, SBUF("Owner: "));
        else if (field_id == 0x80003UL) append(APPENDPARAMS, SBUF("Destination: "));
        else if (field_id == 0x80004UL) append(APPENDPARAMS, SBUF("Issuer: "));
        else if (field_id == 0x80005UL) append(APPENDPARAMS, SBUF("Authorize: "));
        else if (field_id == 0x80006UL) append(APPENDPARAMS, SBUF("Unauthorize: "));
        else if (field_id == 0x80007UL) append(APPENDPARAMS, SBUF("Target: "));
        else if (field_id == 0x80008UL) append(APPENDPARAMS, SBUF("RegularKey: "));
        else if (field_id == 0x120001UL) append(APPENDPARAMS, SBUF("Paths: "));
        else if (field_id == 0x130001UL) append(APPENDPARAMS, SBUF("Indexes: "));
        else if (field_id == 0x130002UL) append(APPENDPARAMS, SBUF("Hashes: "));
        else if (field_id == 0x130003UL) append(APPENDPARAMS, SBUF("Amendments: "));
        else if (field_id == 0xe0002UL) append(APPENDPARAMS, SBUF("TransactionMetaData: "));
        else if (field_id == 0xe0003UL) append(APPENDPARAMS, SBUF("CreatedNode: "));
        else if (field_id == 0xe0004UL) append(APPENDPARAMS, SBUF("DeletedNode: "));
        else if (field_id == 0xe0005UL) append(APPENDPARAMS, SBUF("ModifiedNode: "));
        else if (field_id == 0xe0006UL) append(APPENDPARAMS, SBUF("PreviousFields: "));
        else if (field_id == 0xe0007UL) append(APPENDPARAMS, SBUF("FinalFields: "));
        else if (field_id == 0xe0008UL) append(APPENDPARAMS, SBUF("NewFields: "));
        else if (field_id == 0xe0009UL) append(APPENDPARAMS, SBUF("TemplateEntry: "));
        else if (field_id == 0xe000aUL) append(APPENDPARAMS, SBUF("Memo: "));
        else if (field_id == 0xe000bUL) append(APPENDPARAMS, SBUF("SignerEntry: "));
        else if (field_id == 0xe000cUL) append(APPENDPARAMS, SBUF("EmitDetails: "));
        else if (field_id == 0xe0010UL) append(APPENDPARAMS, SBUF("Signer: "));
        else if (field_id == 0xe0012UL) append(APPENDPARAMS, SBUF("Majority: "));
        else if (field_id == 0xe0013UL) append(APPENDPARAMS, SBUF("NegativeUNLEntry: "));
        else if (field_id == 0xf0002UL) append(APPENDPARAMS, SBUF("SigningAccounts: "));
        else if (field_id == 0xf0003UL) append(APPENDPARAMS, SBUF("Signers: "));
        else if (field_id == 0xf0004UL) append(APPENDPARAMS, SBUF("SignerEntries: "));
        else if (field_id == 0xf0005UL) append(APPENDPARAMS, SBUF("Template: "));
        else if (field_id == 0xf0006UL) append(APPENDPARAMS, SBUF("Necessary: "));
        else if (field_id == 0xf0007UL) append(APPENDPARAMS, SBUF("Sufficient: "));
        else if (field_id == 0xf0008UL) append(APPENDPARAMS, SBUF("AffectedNodes: "));
        else if (field_id == 0xf0009UL) append(APPENDPARAMS, SBUF("Memos: "));
        else if (field_id == 0xf0010UL) append(APPENDPARAMS, SBUF("Majorities: "));
        else if (field_id == 0xf0011UL) append(APPENDPARAMS, SBUF("NegativeUNL: "));


        // fixed length types including amount
        if (type_code == 18)
        {
            // pathset
        }
        else if (type_code == 14)
        {   // object
            if (field_code == 1)
            {
                indent_level--;
                object_level--;
                append(APPENDPARAMS, SBUF("}"));
            }
            else
            {
                append(APPENDNOINDENT, SBUF("{\n"));
                object_level++;
                indent_level++;
                nocomma = 1;
            }
        }
        else if (type_code == 15)
        {   // array
            if (field_code == 1)
            {
                indent_level--;
                array_level--;
                append(APPENDPARAMS, SBUF("]"));
            }
            else
            {
                append(APPENDPARAMS, SBUF("[\n"));
                array_level++;
                indent_level++;
                nocomma = 1;
            }
        }
        else if (type_code == 8) // account
        {

         //   printf("upto: %d, remaining: %d\n", upto, remaining);
            REQUIRE(20);

            char acc[64];
            size_t acc_size = 64;
            if (!b58check_enc(acc, &acc_size, 0, n + 1, 20))
            {
                fprintf(stderr, "Error: could not base58 encode\n");
                return 0;
            }
            acc[0] = 'r';
            append(APPENDNOINDENT, SBUF("\""));
            append(APPENDNOINDENT, acc, acc_size);
            append(APPENDNOINDENT, SBUF("\""));
            ADVANCE(21);
        }
        else if (type_code == 4 || type_code == 5 || type_code == 17)
        {
            // uint128, uint256, uint160
            REQUIRE(size-1);
            
            append(APPENDNOINDENT, SBUF("\""));
            char hexout[513];
            HEX(hexout, n, size);
            append(APPENDNOINDENT, hexout, size*2);
            append(APPENDNOINDENT, SBUF("\""));

            ADVANCE(size);    
        }
        else if (type_code == 7 || type_code == 19) // blob
        {
            int64_t field_len = *n;
            if (field_len <= 129)
            {
                // one byte size
                ADVANCE(1);
            }
            else if (field_len <= 12480)
            {
                // two byte size
                REQUIRE(1);
                field_len = 193 + ((field_len - 193) * 256) + *n;
                ADVANCE(2);
            }
            else
            {
                // three byte size
                REQUIRE(2);
                field_len = 12481 + ((field_len - 241) * 0xFFFFU) + ((*(n+1)) * 256) + *(n+2);
                ADVANCE(3);
            }

//                printf("vl len: %d\n", field_len);
            REQUIRE(field_len-1);

            append(APPENDNOINDENT, SBUF("\""));
            char hexout[1024];
            int already_printed = 0;
            int to_print = field_len - already_printed;
            do
            {
                if (to_print > sizeof(hexout)/2)
                    to_print = sizeof(hexout)/2;
                HEX(hexout, n + already_printed, to_print);
                append(APPENDNOINDENT, hexout, to_print*2);
                already_printed += to_print;
                to_print = field_len - already_printed;
            } while (to_print > 0);

            append(APPENDNOINDENT, SBUF("\""));

            ADVANCE(field_len);
        }
        else if (type_code == 6) // amount
        {
            // issued currency
            if ((*n) >> 7U)
            {
                size = 48U;
                REQUIRE(47);
                uint16_t exponent = (((uint16_t)(*n)) << 8U) +
                                    (uint16_t)(*(n+1));

                exponent &= 0b0011111111000000;
                exponent >>= 6U;
                
                char str[1024];
                int is_neg = (((*n) >> 6U) & 1U == 0);

                uint64_t mantissa = 
                    (((uint64_t)((*(n+1) & 0b111111))) << 48U) +
                    (((uint64_t)((*(n+2)))) << 40U) +
                    (((uint64_t)((*(n+3)))) << 32U) +
                    (((uint64_t)((*(n+4)))) << 24U) +
                    (((uint64_t)((*(n+5)))) << 16U) +
                    (((uint64_t)((*(n+6)))) <<  8U) +
                    (((uint64_t)((*(n+7)))) <<  0U);


                // currency code = n + 8 ... n + 27
                // issuer = n + 28 ... n + 47

                int ascii = 1;
                for (int i = 0; i < 12; ++i)
                {
                    if (*(n + 8 + i) != 0)
                    {
                        ascii = 0;
                        break;
                    }
                }

                if (ascii)
                for (int i = 12; i < 15; ++i)
                {
                    char x = *(n + 8 + i);
                    if (x >= 'a' && x <= 'z')
                        continue;
                    if (x >= 'A' && x <= 'Z')
                        continue;
                    if (x >= '0' && x <= '9')
                        continue;
                    ascii = 0;
                    break;
                }

                if (ascii)
                for (int i = 15; i < 20; ++i)
                {
                    if (*(n + 8 + i) != 0)
                    {
                        ascii = 0;
                        break;
                    }
                }

                char issuer[64];
                size_t issuer_size = 64;
                if (!b58check_enc(issuer, &issuer_size, 0, n + 28, 20))
                {
                    fprintf(stderr, "Error: could not base58 encode\n");
                    return 0;
                }
                issuer[0] = 'r';

                char currency[41];
                if (ascii)
                {
                    for (int i = 0; i < 3; ++i)
                        currency[i] = (char)(*(n + 8 + 12 + i));
                    currency[3] = '\0';
                }
                else
                {
                    for (int i = 0; i < 20; ++i)
                    {
                        unsigned char hi = (*(n+8+i)) >> 4U;
                        unsigned char lo = (*(n+8+i)) & 0xFU;
                        hi += (hi > 9 ? 'A' - 10 : '0');
                        lo += (lo > 9 ? 'A' - 10 : '0');
                        currency[i*2+0] = (char)hi;
                        currency[i*2+1] = (char)lo;
                    }
                }

                int32_t exp = (int32_t)(exponent);
                exp -= 97;

                append(APPENDNOINDENT, SBUF("{\n"));
                snprintf(str, 1024, "\tAmount: \"%s%lluE%d\",\n", (is_neg ? "-" : ""), mantissa, exp);
                append(APPENDPARAMS, str, 1024);
                append(APPENDPARAMS, SBUF("\tCurrency: \""));
                append(APPENDNOINDENT, SBUF(currency));
                append(APPENDNOINDENT, SBUF("\",\n"));
                append(APPENDPARAMS, SBUF("\tIssuer: \""));
                append(APPENDNOINDENT, SBUF(issuer));
                append(APPENDNOINDENT, SBUF("\"\n"));
                append(APPENDPARAMS, SBUF("}"));

                ADVANCE(48);
            }
            else
            {

                REQUIRE(7);
                // xrp (drops) currency
                char str[24];
                char* s = str;
                // handle negative sign bit
                if ((*n) >> 6U == 0)
                    *s++ = '-';
            
                uint64_t number =  
                    ((uint64_t)((*n) & 0b111111U) << 56U) + 
                    ((uint64_t)(*(n+1)) << 48U) + 
                    ((uint64_t)(*(n+2)) << 40U) + 
                    ((uint64_t)(*(n+3)) << 32U) + 
                    ((uint64_t)(*(n+4)) << 24U) + 
                    ((uint64_t)(*(n+5)) << 16U) + 
                    ((uint64_t)(*(n+6)) <<  8U) + 
                    ((uint64_t)(*(n+7)) <<  0U);
               
                snprintf(s, 23, "%llu", number); 
           
                

                append(APPENDNOINDENT, str, 16);

                ADVANCE(8);
            }
        }
        else if (type_code == 1 || type_code == 2 || type_code == 3 || type_code == 16) // uint16
        {
            uint64_t number = 0;
            if (type_code == 1) // uint16
            {
                REQUIRE(1);
                number =  (((uint64_t)(*(n+0))) << 8U) + 
                          (((uint64_t)(*(n+1))) << 0U);
                ADVANCE(2);
            }
            else if (type_code == 2) // uint32
            {
                REQUIRE(3);
                number = 
                    (((uint64_t)(*(n+0))) << 24U) +
                    (((uint64_t)(*(n+1))) << 16U) +
                    (((uint64_t)(*(n+2))) << 8U ) +
                    (((uint64_t)(*(n+3))) << 0U );

                ADVANCE(4);
            }
            else if (type_code == 3) // uint64
            {
                REQUIRE(7);
                number = 
                    (((uint64_t)(*(n+0))) << 56U) +
                    (((uint64_t)(*(n+1))) << 48U) +
                    (((uint64_t)(*(n+2))) << 40U ) +
                    (((uint64_t)(*(n+3))) << 32U ) +
                    (((uint64_t)(*(n+4))) << 24U) +
                    (((uint64_t)(*(n+5))) << 16U) +
                    (((uint64_t)(*(n+6))) << 8U ) +
                    (((uint64_t)(*(n+7))) << 0U );
                ADVANCE(8);
            }
            else // uint8
            {
                number = *n;
                ADVANCE(1);
            }
            char str[16];
            snprintf(str, 16, "%lu", number);
            append(APPENDNOINDENT, str, 16);
        }
    }

    

    indent_level--;
    append(APPENDNOINDENT, SBUF("\n"));
    append(APPENDPARAMS, SBUF("}\n"));

    return 1;
}

int main(int argc, char** argv)
{
    b58_sha256_impl = calc_sha_256;
    if (argc != 2)
        return fprintf(stderr, "Usage: %s <HEX BLOB>\n", argv[0]);


    // hex conversion
    int hexlen = strlen(argv[1]);
    if (hexlen % 2 == 1)
        return fprintf(stderr, "Hex length must be even\n");

    int len = hexlen/2;
    uint8_t* rawbytes = malloc(len);
    uint8_t* rawupto = rawbytes;
    int error = 0;
    for (char* x = argv[1]; *x;  x+=2)
    {
        uint8_t hi = *x;
        uint8_t lo = *(x+1);

        hi =    (hi >= 'A' && hi <= 'F' ? hi - 'A' + 10 :
                (hi >= 'a' && hi <= 'f' ? hi - 'a' + 10 : 
                (hi >= '0' && hi <= '9' ? hi - '0' :
                 (error=1) )));
    
        lo =    (lo >= 'A' && lo <= 'F' ? lo - 'A' + 10 :
                (lo >= 'a' && lo <= 'f' ? lo - 'a' + 10 : 
                (lo >= '0' && lo <= '9' ? lo - '0' :
                 (error=1) )));

        *rawupto++ = (hi << 4U) + lo;
    }

    if (error)
        return fprintf(stderr, "Non-hex nibble detected\n");

    uint8_t* output = 0;
    if (!deserialize(&output, rawbytes, len))
        return fprintf(stderr, "Could not deserialize\n");

    printf("%s\n", output);

    return 0;
}