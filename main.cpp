#include <stdio.h>
#include <conio.h>
#include <unistd.h>
#include <windows.h>
#include <string>
#include <tinyxml2.h>

#include "cd.h"
#include "cdreader.h"

namespace param {

    char*	isoFile=NULL;
    std::string	outPath;
    std::string xmlFile;

    int		printOnly=false;

}

namespace global {

    std::string isoPath;

}

void PrintId(char* text) {

    int i=0;

    while(text[i] != 32) {
        printf("%c", text[i]);
        i++;
    }
    printf("\n");

}

void PrintDate(char* text) {

    for(int i=0; i<17; i++) {
        printf("%c", text[i]);
    }
    printf("\n");

}

void BackDir(std::string& path) {

    path.resize(path.rfind("/"));

}

const char* CleanVolumeId(const char* id) {

    static char buff[38];
    int i;

    for(i=0; (i<37)&&(id[i]!=0x20); i++) {
        buff[i] = id[i];
    }
    buff[i] = 0x00;

    return buff;

}

const char* CleanIdentifier(const char* id) {

    static char buff[16];
    int i;

    for(i=0; (id[i]!=0x00)&&(id[i]!=';'); i++) {
        buff[i] = id[i];
    }
    buff[i] = 0x00;

    return buff;

}

void ParseDirectories(cd::IsoReader& reader, int offs, tinyxml2::XMLDocument* doc, tinyxml2::XMLElement* element) {

    cd::IsoDirEntries dirEntries;
    tinyxml2::XMLElement* newelement = NULL;
    std::string outputPath;
    FILE *outFile;

    int entriesFound = dirEntries.ReadDirEntries(&reader, offs);
    dirEntries.SortByLBA();

    for(int e=2; e<entriesFound; e++) {

        if (dirEntries.dirEntryList[e].flags & 0x2) {

            global::isoPath += "/";
            global::isoPath += dirEntries.dirEntryList[e].identifier;

            if (element != NULL) {

                newelement = doc->NewElement("dir");
                newelement->SetAttribute("name", dirEntries.dirEntryList[e].identifier);
                element->InsertEndChild(newelement);

            }

            ParseDirectories(reader, dirEntries.dirEntryList[e].entryOffs.lsb, doc, newelement);

            BackDir(global::isoPath);

        } else {

			outputPath = global::isoPath;

			if (!outputPath.empty()) {
				outputPath.erase(0, 1);
				outputPath += "/";
			}

			outputPath = outputPath + CleanIdentifier(dirEntries.dirEntryList[e].identifier);

			printf("   Extracting %s...\n", dirEntries.dirEntryList[e].identifier);

			outputPath = param::outPath + outputPath;

            if (element != NULL) {

                newelement = doc->NewElement("file");
                newelement->SetAttribute("name", CleanIdentifier(dirEntries.dirEntryList[e].identifier));
                newelement->SetAttribute("source", outputPath.c_str());

            }

			if ((((cd::ISO_XA_ATTRIB*)dirEntries.dirEntryList[e].extData)->attributes&0xff)&0x08) {

				// Extract regular file

				if (element != NULL)
                    newelement->SetAttribute("type", "data");

				reader.SeekToSector(dirEntries.dirEntryList[e].entryOffs.lsb);

				outFile = fopen(outputPath.c_str(), "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %s...", outputPath.c_str());
					return;
				}

				int bytesLeft = dirEntries.dirEntryList[e].entrySize.lsb;
				while(bytesLeft > 0) {

					u_char copyBuff[2048];
					int bytesToRead = bytesLeft;

					if (bytesToRead > 2048)
						bytesToRead = 2048;

					reader.ReadBytes(copyBuff, bytesToRead);
					fwrite(copyBuff, 1, bytesToRead, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);

			} else {

				// Extract XA

				int isSTR = false;

				{

					char readBuff[12];
					reader.SeekToSector(dirEntries.dirEntryList[e].entryOffs.lsb);
					reader.ReadBytesXA(readBuff, 12);

					// If first sector has a data subheader, then its definitely a STR file
					if (!(readBuff[2] & 0x4))
						isSTR = true;

				}

				int bytesLeft;

				if (isSTR) {

					if (element != NULL)
						newelement->SetAttribute("type", "str");

					bytesLeft = dirEntries.dirEntryList[e].entrySize.lsb;	// For STR video streams

				} else {

					if (element != NULL)
						newelement->SetAttribute("type", "xa");

					bytesLeft = 2336*(dirEntries.dirEntryList[e].entrySize.lsb/2048);	// For XA audio streams

				}


				reader.SeekToSector(dirEntries.dirEntryList[e].entryOffs.lsb);

				outFile = fopen(outputPath.c_str(), "wb");

				if (outFile == NULL) {
					printf("ERROR: Cannot create file %s...", outputPath.c_str());
					return;
				}

				// Copy loop
				while(bytesLeft > 0) {

					u_char copyBuff[2336];

					int bytesToRead = bytesLeft;

					if (bytesToRead > 2336)
						bytesToRead = 2336;

					reader.ReadBytesXA(copyBuff, 2336);

					fwrite(copyBuff, 1, 2336, outFile);

					bytesLeft -= bytesToRead;

				}

				fclose(outFile);


			}

			if (element != NULL)
				element->InsertEndChild(newelement);

        }

    }

}

void ParseISO(cd::IsoReader& reader) {

    cd::ISO_DESCRIPTOR	descriptor;

    reader.SeekToSector(16);
    reader.ReadBytes(&descriptor, 2048);


    printf("ISO decriptor:\n\n");

    printf("   System ID      : ");
    PrintId(descriptor.systemID);
    printf("   Volume ID      : ");
    PrintId(descriptor.volumeID);
    printf("   Volume Set ID  : ");
    PrintId(descriptor.volumeSetIdentifier);
    printf("   Publisher ID   : ");
    PrintId(descriptor.publisherIdentifier);
    printf("   Data Prep. ID  : ");
    PrintId(descriptor.dataPreparerIdentifier);
    printf("   Application ID : ");
    PrintId(descriptor.applicationIdentifier);
    printf("\n");

    printf("   Volume Create Date : ");
    PrintDate(descriptor.volumeCreateDate);
    printf("   Volume Modify Date : ");
    PrintDate(descriptor.volumeModifyDate);
    printf("   Volume Expire Date : ");
    PrintDate(descriptor.volumeExpiryDate);
    printf("\n");

    cd::IsoPathTable pathTable;

    int numEntries = pathTable.ReadPathTable(&reader, descriptor.pathTable1Offs);

    if (numEntries == 0) {
        printf("   No files to find.\n");
        return;
    }

    if (!param::outPath.empty()) {

        if (param::outPath.rfind("/") != param::outPath.length()-1);
            param::outPath += "/";

    }

	// Prepare output directories
	for(int i=1; i<numEntries; i++) {

		char pathBuff[256];
		pathTable.GetFullDirPath(i, pathBuff, 256);

		std::string dirPath = param::outPath;

		dirPath += pathBuff;
		mkdir(dirPath.c_str());

	}

    printf("ISO contents:\n\n");

    tinyxml2::XMLDocument xmldoc;

    if (!param::xmlFile.empty()) {

		tinyxml2::XMLElement *baseElement = xmldoc.NewElement("iso_project");

		tinyxml2::XMLElement *trackElement = xmldoc.NewElement("track");
		trackElement->SetAttribute("type", "data");

		tinyxml2::XMLElement *newElement = xmldoc.NewElement("identifiers");

		if (descriptor.systemID[0] != 0x20)
			newElement->SetAttribute("system", CleanVolumeId(descriptor.systemID));
		if (descriptor.applicationIdentifier[0] != 0x20)
			newElement->SetAttribute("application", CleanVolumeId(descriptor.applicationIdentifier));
		if (descriptor.volumeID[0] != 0x20)
			newElement->SetAttribute("volume", CleanVolumeId(descriptor.volumeID));
		if (descriptor.volumeSetIdentifier[0] != 0x20)
			newElement->SetAttribute("volume_set", CleanVolumeId(descriptor.volumeSetIdentifier));
		if (descriptor.publisherIdentifier[0] != 0x20)
			newElement->SetAttribute("publisher", CleanVolumeId(descriptor.publisherIdentifier));
		if (descriptor.dataPreparerIdentifier[0] != 0x20)
			newElement->SetAttribute("data_preparer", CleanVolumeId(descriptor.dataPreparerIdentifier));

		trackElement->InsertEndChild(newElement);
		newElement = xmldoc.NewElement("directory_tree");

		ParseDirectories(reader, pathTable.pathTableList[0].dirOffs, &xmldoc, newElement);

		trackElement->InsertEndChild(newElement);
		baseElement->InsertEndChild(trackElement);
		xmldoc.InsertEndChild(baseElement);
		xmldoc.SaveFile(param::xmlFile.c_str());

    } else {

    	ParseDirectories(reader, pathTable.pathTableList[0].dirOffs, &xmldoc, NULL);

    }

}

int main(int argc, char *argv[]) {


    printf("isodump v0.25a - PlayStation ISO dumping tool\n");
    printf("2017 Meido-Tek Productions (Lameguy64).\n\n");

	if (argc == 1) {

		printf("Usage:\n\n");
		printf("   isodump <isofile> [-x <path>]\n\n");
		printf("   <isofile>   - File name of ISO file (supports any 2352 byte/sector images).\n");
		printf("   [-x <path>] - Specified destination directory of extracted files.\n");
		printf("   [-s <path>] - Outputs an MKPSXISO compatible XML script for later rebuilding.\n");

		return(EXIT_SUCCESS);

	}


    for(int i=1; i<argc; i++) {

		// Is it a switch?
        if (argv[i][0] == '-') {

			// Directory path
            if (strcmp("x", &argv[i][1]) == 0) {

                param::outPath = argv[i+1];

                i++;

            } else if (strcmp("xml", &argv[i][1]) == 0) {

                param::xmlFile = argv[i+1];
                i++;

            } else if (strcmp("p", &argv[i][1]) == 0) {

            	param::printOnly = true;

            } else {

            	printf("Unknown parameter: %s\n", argv[i]);
            	return(EXIT_FAILURE);

            }

        } else {

			if (param::isoFile == NULL) {

				param::isoFile = argv[i];

			} else {

				printf("Only one iso file is supported.\n");
				return(EXIT_FAILURE);

			}

        }

    }

	if (param::isoFile == NULL) {

		printf("No iso file specified.\n");
		return(EXIT_FAILURE);

	}


	cd::IsoReader reader;

	if (!reader.Open(param::isoFile)) {

		printf("ERROR: Cannot open file %s...\n", param::isoFile);
		return(EXIT_FAILURE);

	}

	if (!param::outPath.empty())
		printf("Output directory : %s\n", param::outPath.c_str());

    ParseISO(reader);

    reader.Close();

    exit(EXIT_SUCCESS);

}
