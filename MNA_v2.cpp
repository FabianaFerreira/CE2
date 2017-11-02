/*
Elementos aceitos e linhas do netlist:

Resistor:                     R<nome> <no+> <no-> <resistencia>
VCCS:                         G<nome> <io+> <io-> <vi+> <vi-> <transcondutancia>
VCVS:                         E<nome> <vo+> <vo-> <vi+> <vi-> <ganho de tensao>
CCCS:                         F<nome> <io+> <io-> <ii+> <ii-> <ganho de corrente>
CCVS:                         H<nome> <vo+> <vo-> <ii+> <ii-> <transresistencia>
Fonte I:                      I<nome> <no+> <no-> <parametros>
Fonte V:                      V<nome> <no+> <no-> <parametros>
Amp. op.:                     O<nome> <vo1> <vo2> <vi1> <vi2>
Resistor linear por partes:   N<nome> <no+> <no-> <4 pontos vi, ji>
Capacitor:                    C<nome> <no1> <no2> <capacitancia>
Indutor:                      L<nome> <no1> <no2> <indutancia>
Transformador ideal:          K<nome> <noa> <nob> <noc> <nod> <n>
Chave:                        $<nome> <noa> <nob> <noContc> <noContd> <gon> <goff> <vref>
Comentario:                   *<comentario>


Parametros:
Fonte contínua: DC <valor>
Fonte senoidal: SIN <nível contínuo> <amplitude> <frequência em Hz> <atraso> <amortecimento> <defasagem em graus> <número de ciclos>
Fonte pulsada: PULSE <amplitude 1> <amplitude 2> <atraso> <tempo de subida> <tempo de descida> <tempo ligada> <período> <número de ciclos>

As fontes F e H tem o ramo de entrada em curto
O amplificador operacional ideal tem a saida suspensa
Os nos podem ser nomes
*/

#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#define versao "1.0"
#define MAX_LINHA 80
#define MAX_TIPO_FONTE  5
#define MAX_NOME 11
#define MAX_ELEM 50
#define MAX_NOS 50
#define TOLG 1e-9
#define PO_CAPACITOR  1e9
#define PO_INDUTOR    1e-9
//#define DEBUG
#define PI acos(-1.0)
#define NOME_ARQUIVO_SAIDA "saida_simulacao.tab"

typedef struct sine /* STRUCT SIN */
{
  double nivel_dc,
         amplitude,
         freq,
         atraso,
         amortecimento,
         defasagem;
  unsigned int ciclos;
} sine;

typedef struct dc /* STRUCT DC */
{
  double valor;
} dc;

typedef struct pulse /* STRUCT PULSE */
{
  double amplitude1,
         amplitude2,
         atraso,
         tempo_subida,
         tempo_descida,
         tempo_ligada,
         periodo;
  unsigned int ciclos;
} pulse;

/*Elemento possui atributos de fontes, pois há tipos diferentes, com parametros diferentes*/
typedef struct elemento /* Elemento do netlist */
{
  char nome[MAX_NOME];
  double valor;
  char tipo_fonte[MAX_TIPO_FONTE];
  int a,b,c,d,x,y;
  sine fonte_seno;
  dc fonte_dc;
  pulse fonte_pulso;
  double jt0, vt0;
} elemento;

/*As seguintes variaveis vao definir os passos e o tempo de simulacao a ser usado
  Como o passo a ser escrito no arquivo de saida pode nao ser o mesmo do passo da
  integracao, vamos definir os dois separadamente*/
double tempo_simulacao, passo_simulacao, passo_saida;
double tempo_atual;

elemento netlist[MAX_ELEM]; /* Netlist */
int elementosVariantes[MAX_ELEM];
int elementosNaoLineares[MAX_ELEM];

int
  numeroElementos, /* Elementos */
  numeroVariaveis, /* Variaveis */
  numeroNos, /* Nos */
  i,j,k;

unsigned contadorElementosVariantes;
unsigned contadorElementosNaoLineares;

char
/* Foram colocados limites nos formatos de leitura para alguma protecao
   contra excesso de caracteres nestas variaveis */
  nomeArquivo[MAX_LINHA+1],
  tipo,
  na[MAX_NOME],nb[MAX_NOME],nc[MAX_NOME],nd[MAX_NOME],
  lista[MAX_NOS+1][MAX_NOME+2], /*Tem que caber jx antes do nome */
  txt[MAX_LINHA+1],
  *p;

FILE *arquivo;

double
  g,
  Yn[MAX_NOS+1][MAX_NOS+2],
  YnAnterior[MAX_NOS+1][MAX_NOS+2],
  YnInvariantes[MAX_NOS+1][MAX_NOS+2],
  solucaoAnterior[MAX_NOS+2];

/*  Rotina para Resolucao de sistema de equacoes lineares.
   Metodo de Gauss-Jordan com condensacao pivotal */
int ResolverSistema(void)
{
  int i,j,l, a;
  double t, p;

  for (i=1; i<=numeroVariaveis; i++) {
    t=0.0;
    a=i;
    for (l=i; l<=numeroVariaveis; l++) {
      if (fabs(Yn[l][i])>fabs(t)) {
	a=l;
	t=Yn[l][i];
      }
    }
    if (i!=a) {
      for (l=1; l<=numeroVariaveis+1; l++) {
	p=Yn[i][l];
	Yn[i][l]=Yn[a][l];
	Yn[a][l]=p;
      }
    }
    if (fabs(t)<TOLG) {
      printf("Sistema singular\n");
      return 1;
    }
    for (j=numeroVariaveis+1; j>0; j--) {  /* Basta j>i em vez de j>0 */
      Yn[i][j]/= t;
      p=Yn[i][j];
      if (p!=0)  /* Evita operacoes com zero */
        for (l=1; l<=numeroVariaveis; l++) {
	  if (l!=i)
	    Yn[l][j]-=Yn[l][i]*p;
        }
    }
  }
  return 0;
}

/* Rotina que conta os nos e atribui numeros a eles */
int NumerarNo(char *nome)
{
  int i,achou;
  i=0; achou=0;
  while (!achou && i<=numeroVariaveis)
    if (!(achou=!strcmp(nome,lista[i]))) i++;
  if (!achou) {
    if (numeroVariaveis==MAX_NOS) {
      printf("O programa so aceita ate %d nos\n",numeroVariaveis);
      exit(1);
    }
    numeroVariaveis++;
    strcpy(lista[numeroVariaveis],nome);
    return numeroVariaveis; /* novo no */
  }
  else {
    return i; /* no ja conhecido */
  }
}

void ArmazenarResultadoAnterior()
{
  // for (i=0; i<=numeroVariaveis; i++)
  // {
  //   for (j=0; j<=numeroVariaveis+1; j++)
  //     YnAnterior[i][j] = Yn[i][j];
  // }
  for (i=0; i<=numeroVariaveis+1; i++)
    solucaoAnterior[i] = Yn[i][numeroVariaveis+1]; /*pega a ultima coluna de Yn: solucao do sistema*/
}

void CopiarEstampaInvariante()
{
  for (i=0; i<=numeroVariaveis; i++)
  {
    for (j=0; j<=numeroVariaveis+1; j++)
      Yn[i][j] = YnInvariantes[i][j];
  }
}

void ZerarSistema()
{
  for (i=0; i<=numeroVariaveis; i++)
  {
    for (j=0; j<=numeroVariaveis+1; j++)
      Yn[i][j]=0;
  }
}

void MostrarSistema ()
{
  for (k=1; k<=numeroVariaveis; k++)
  {
  	for (j=1; j<=numeroVariaveis+1; j++)
  		if (Yn[k][j]!=0)
  			printf("%+4.3f ",Yn[k][j]);
  		else printf(" ..... ");
  	printf("\n");
  }
  getch();
}

void MostrarSolucaoAtual ()
{
  	for (j=1; j<=numeroVariaveis; j++)
  	 printf("%+4.3f ",Yn[j][numeroVariaveis+1]);
  	printf("\n");
  getch();
}

void LerNetlist (FILE *arquivo)
{
  contadorElementosVariantes = 0;
  contadorElementosNaoLineares = 0;
  printf("Lendo netlist:\n");
  fgets(txt,MAX_LINHA,arquivo);
  printf("Titulo: %s",txt);
  while (fgets(txt,MAX_LINHA,arquivo))
  {
    numeroElementos++; /* Nao usa o netlist[0] */
    if (numeroElementos > MAX_ELEM)
    {
      printf("O programa so aceita ate %d elementos\n",MAX_ELEM);
      exit(1);
    }
    txt[0]=toupper(txt[0]);
    tipo=txt[0];
    sscanf(txt,"%10s",netlist[numeroElementos].nome);
    p=txt+strlen(netlist[numeroElementos].nome); /* Inicio dos parametros */
    /* O que e lido depende do tipo */

    /*RESISTOR*/
    if (tipo=='R')
    {
      sscanf(p,"%10s%10s%lg",na,nb,&netlist[numeroElementos].valor);
      printf("%s %s %s %g\n",netlist[numeroElementos].nome,na,nb,netlist[numeroElementos].valor);
      netlist[numeroElementos].a=NumerarNo(na);
      netlist[numeroElementos].b=NumerarNo(nb);
    }
    /*CAPACITOR*/
    else if (tipo=='C')
    {
      sscanf(p,"%10s%10s%lg",na,nb,&netlist[numeroElementos].valor);
      printf("%s %s %s %g\n",netlist[numeroElementos].nome,na,nb,netlist[numeroElementos].valor);
      netlist[numeroElementos].a=NumerarNo(na);
      netlist[numeroElementos].b=NumerarNo(nb);
      elementosVariantes[contadorElementosVariantes] = numeroElementos;
      contadorElementosVariantes++;
    }
    /*INDUTOR*/
    else if (tipo=='L')
    {
      sscanf(p,"%10s%10s%lg",na,nb,&netlist[numeroElementos].valor);
      printf("%s %s %s %g\n",netlist[numeroElementos].nome,na,nb,netlist[numeroElementos].valor);
      netlist[numeroElementos].a=NumerarNo(na);
      netlist[numeroElementos].b=NumerarNo(nb);
      elementosVariantes[contadorElementosVariantes] = numeroElementos;
      contadorElementosVariantes++;
    }

    else if (tipo == 'I' || tipo == 'V')
    {
      sscanf(p,"%10s%10s%5s",na,nb,netlist[numeroElementos].tipo_fonte);

      if (strcmp(netlist[numeroElementos].tipo_fonte, "DC") == 0)
      {
        /*valor*/
        sscanf(p, "%*10s%*10s%*5s%lg", &netlist[numeroElementos].fonte_dc.valor);
      }
      else if (strcmp(netlist[numeroElementos].tipo_fonte, "SIN") == 0)
      {
        /*nivel_dc, amplitude, freq, atraso, amortecimento, defasagem, ciclos;*/
        sscanf(p, "%*10s%*10s%*5s%lg%lg%lg%lg%lg%lg%i", &netlist[numeroElementos].fonte_seno.nivel_dc,
                &netlist[numeroElementos].fonte_seno.amplitude, &netlist[numeroElementos].fonte_seno.freq,
                &netlist[numeroElementos].fonte_seno.atraso, &netlist[numeroElementos].fonte_seno.amortecimento,
                &netlist[numeroElementos].fonte_seno.defasagem, &netlist[numeroElementos].fonte_seno.ciclos);
        elementosVariantes[contadorElementosVariantes] = numeroElementos;
        contadorElementosVariantes++;
      }
      else if (strcmp(netlist[numeroElementos].tipo_fonte, "PULSE") == 0)
      {
        /*amplitude1, amplitude2, atraso, tempo_subida,tempo_descida,tempo_ligada, periodo, ciclos;*/
        sscanf(p, "%*10s%*10s%*5s%lg%lg%lg%lg%lg%lg%lg%i", &netlist[numeroElementos].fonte_pulso.amplitude1,
                &netlist[numeroElementos].fonte_pulso.amplitude2, &netlist[numeroElementos].fonte_pulso.atraso,
                &netlist[numeroElementos].fonte_pulso.tempo_subida, &netlist[numeroElementos].fonte_pulso.tempo_descida,
                &netlist[numeroElementos].fonte_pulso.tempo_ligada, &netlist[numeroElementos].fonte_pulso.periodo,
                &netlist[numeroElementos].fonte_pulso.ciclos);
        elementosVariantes[contadorElementosVariantes] = numeroElementos;
        contadorElementosVariantes++;
      }
      netlist[numeroElementos].a=NumerarNo(na);
      netlist[numeroElementos].b=NumerarNo(nb);
    }

    /*FONTES CONTROLADAS*/
    else if (tipo=='G' || tipo=='E' || tipo=='F' || tipo=='H')
    {
      sscanf(p,"%10s%10s%10s%10s%lg",na,nb,nc,nd,&netlist[numeroElementos].valor);
      printf("%s %s %s %s %s %g\n",netlist[numeroElementos].nome,na,nb,nc,nd,netlist[numeroElementos].valor);
      netlist[numeroElementos].a=NumerarNo(na);
      netlist[numeroElementos].b=NumerarNo(nb);
      netlist[numeroElementos].c=NumerarNo(nc);
      netlist[numeroElementos].d=NumerarNo(nd);
    }
    /*AMPLIFICADOR OPERACIONAL IDEAL*/
    else if (tipo=='O')
    {
      sscanf(p,"%10s%10s%10s%10s",na,nb,nc,nd);
      printf("%s %s %s %s %s\n",netlist[numeroElementos].nome,na,nb,nc,nd);
      netlist[numeroElementos].a=NumerarNo(na);
      netlist[numeroElementos].b=NumerarNo(nb);
      netlist[numeroElementos].c=NumerarNo(nc);
      netlist[numeroElementos].d=NumerarNo(nd);
    }
    /*TRANSFORMADOR IDEAL*/
    else if(tipo=='K')
    {
      sscanf(p,"%10s%10s%10s%10s%lg",na,nb,nc,nd,&netlist[numeroElementos].valor);
      printf("%s %s %s %s %s\n",netlist[numeroElementos].nome,na,nb,nc,nd);
      netlist[numeroElementos].a=NumerarNo(na);
      netlist[numeroElementos].b=NumerarNo(nb);
      netlist[numeroElementos].c=NumerarNo(nc);
      netlist[numeroElementos].d=NumerarNo(nd);
    }
    /* RESISTOR LINEAR POR PARTES */
    else if(tipo=='N')
    {


      elementosNaoLineares[contadorElementosNaoLineares] = numeroElementos;
      contadorElementosNaoLineares ++;
    }
    /* CHAVE */
    else if(tipo=='$')
    {


      elementosNaoLineares[contadorElementosNaoLineares] = numeroElementos;
      contadorElementosNaoLineares ++;
    }
    /* COMENTÁRIO */
    else if (tipo=='*')
    { /* Comentario comeca com "*" */
      printf("Comentario: %s",txt);
      numeroElementos--;
    }

    /*Atribuindo os valores dos passos de integracao e de escrita no arquivo de saida,
      alem do tempo total de simulacao definido no netlist*/
    else if (tipo == '.')
    {
      if (strcmp (netlist[numeroElementos].nome, ".TRAN") == 0)
      {
        sscanf(p, "%lg%lg%*10s%lg", &tempo_simulacao, &passo_simulacao, &passo_saida);
        printf("%lg %lg %lg\n", tempo_simulacao, passo_simulacao, passo_saida);
      }
      numeroElementos--;
    }
    else
    {
      printf("Elemento desconhecido: %s\n",txt);
      getch();
      exit(1);
    }
  }
  fclose(arquivo);
}/*LerNetlist*/

/*Pega apenas as fontes variantes, capacitores e indutores do netlist e monta a estampa de acordo com o tempo atual da simulacao*/
void MontarEstampasVariantes (int elementos[MAX_ELEM], double tempo, unsigned quantidade, double passo_simulacao, unsigned pontoOperacao)
{
  unsigned contador, ciclos, ciclos_passados;
  elemento elementoVariante;
  double amplitude,
         amplitude1,
         amplitude2,
         nivel_dc,
         atraso,
         freq,
         defasagem,
         amortecimento,
         tempo_subida,
         tempo_descida,
         tempo_ligada,
         periodo,
         tempo_normalizado;
    double coefAng,
           coefLin,
           t1,
           t2;

    if (pontoOperacao == 1)
      tempo = 0.0; /*fontes variantes calculadas em 0.0 para ponto de operacao*/

    CopiarEstampaInvariante();

    for (contador = 0; contador < quantidade; contador++)
    {
      elementoVariante = netlist[elementos[contador]];
      /*FONTE DE CORRENTE*/
      if (strcmp(elementoVariante.tipo_fonte, "SIN") == 0)
      {
        amplitude = elementoVariante.fonte_seno.amplitude;
        freq = elementoVariante.fonte_seno.freq;
        atraso = elementoVariante.fonte_seno.atraso;
        defasagem = elementoVariante.fonte_seno.defasagem;
        nivel_dc = elementoVariante.fonte_seno.nivel_dc;
        amortecimento = elementoVariante.fonte_seno.amortecimento;
        ciclos = elementoVariante.fonte_seno.ciclos;
        ciclos_passados = freq*tempo;
        //printf("Ciclos %u Ciclos passados %u\n", ciclos, ciclos_passados);
        if (ciclos_passados >= ciclos)
          elementoVariante.valor = 0;
        else
          elementoVariante.valor = nivel_dc +
                                   amplitude*(exp(-amortecimento*(tempo - atraso)))*(sin(2*PI*freq*(tempo - atraso) + (PI*defasagem)/180));
      }
      /*Tá dando merda, tem que tudo, inclusive os extremos*/
      else if (strcmp(netlist[i].tipo_fonte, "PULSE") == 0)
      {
        amplitude1 = elementoVariante.fonte_pulso.amplitude1;
        amplitude2 = elementoVariante.fonte_pulso.amplitude2;
        atraso = elementoVariante.fonte_pulso.atraso;
        tempo_subida = elementoVariante.fonte_pulso.tempo_subida;
        tempo_descida = elementoVariante.fonte_pulso.tempo_descida;
        ciclos = elementoVariante.fonte_pulso.ciclos;
        periodo = elementoVariante.fonte_pulso.periodo;
        tempo_ligada = elementoVariante.fonte_pulso.tempo_ligada;
        ciclos_passados = unsigned(tempo/periodo);

        /*Tratando descontinuidades*/
        if (tempo_subida == 0)
          tempo_descida = passo_simulacao;
        if (tempo_descida == 0)
          tempo_subida = passo_simulacao;

        tempo_normalizado = tempo - periodo*ciclos_passados;
        /*Achando o valor da fonte no tempo atual*/
        if (ciclos_passados >= ciclos)
          elementoVariante.valor = amplitude1;
        else if (tempo_normalizado <= atraso)
          elementoVariante.valor = amplitude1;
        else if (tempo_normalizado <= tempo_subida + atraso)
        {
          /*Achando a equacao da reta de subida*/
          t1 = atraso;
          t2 = atraso + tempo_subida;
          coefAng = (amplitude2 - amplitude1)/(t2 - t1);
          coefLin = amplitude1 - coefAng*t1;
          elementoVariante.valor = coefAng*tempo + coefLin; /*????????????*/
        }
        else if (tempo_normalizado <= atraso + tempo_subida + tempo_ligada)
          elementoVariante.valor = amplitude2;
        else if (tempo_normalizado <= periodo)
        {
          /*Achando a equacao da reta de descida*/
          t1 = atraso + tempo_subida + tempo_ligada;
          t2 = t1 + tempo_descida;
          coefAng = (amplitude1 - amplitude2)/(t1 - t2);
          coefLin = amplitude1 - coefAng*t1;
          elementoVariante.valor = coefAng*tempo + coefLin;
        }
      }
      g=elementoVariante.valor;
      if (elementoVariante.nome[0] == 'I')
      {
        Yn[elementoVariante.a][numeroVariaveis+1]-=g;
        Yn[elementoVariante.b][numeroVariaveis+1]+=g;
      }

      else if (elementoVariante.nome[0] == 'V')
      {
        Yn[elementoVariante.a][elementoVariante.x]+=1;
        Yn[elementoVariante.b][elementoVariante.x]-=1;
        Yn[elementoVariante.x][elementoVariante.a]-=1;
        Yn[elementoVariante.x][elementoVariante.b]+=1;
        Yn[elementoVariante.x][numeroVariaveis+1]-=g;
      }

      /*VAO DEPENDER DO PASSO E DO VALOR ANTERIOR, SEJA PONTOOP OU RESULTADO DO NEWTON-RAPHSON*/
      /*CAPACITOR*/
      else if (elementoVariante.nome[0]=='C')
      {
        if (pontoOperacao == 1) /*se é análise de ponto de operação*/
        {
          // Vira um R de 1GOhms
          g = 1/PO_CAPACITOR;
          //printf("Condutancia capacitor: %+4.20f\n", g);
    			Yn[elementoVariante.a][elementoVariante.a] += g;
    			Yn[elementoVariante.b][elementoVariante.b] += g;
    			Yn[elementoVariante.a][elementoVariante.b] -= g;
    			Yn[elementoVariante.b][elementoVariante.a] -= g;
        }
        else
        {
          g = (2*elementoVariante.valor)/passo_simulacao;
          Yn[elementoVariante.a][elementoVariante.a] += g;
    			Yn[elementoVariante.b][elementoVariante.b] += g;
    			Yn[elementoVariante.a][elementoVariante.b] -= g;
    			Yn[elementoVariante.b][elementoVariante.a] -= g;
          Yn[elementoVariante.a][numeroVariaveis+1]+= g*(elementoVariante.vt0) + elementoVariante.jt0;
          Yn[elementoVariante.b][numeroVariaveis+1]-= g*(elementoVariante.vt0) + elementoVariante.jt0;
        }
      }

      /*INDUTOR*/
      else if (elementoVariante.nome[0]=='L')
      {
        if (pontoOperacao == 1) /*se é análise de ponto de operação*/
        {
          // Vira um R de 1nOhms
    			g = 1/PO_INDUTOR;
    			Yn[elementoVariante.a][elementoVariante.x] += 1;
    			Yn[elementoVariante.b][elementoVariante.x] -= 1;
    			Yn[elementoVariante.x][elementoVariante.a] -= 1;
    			Yn[elementoVariante.x][elementoVariante.b] += 1;
    			Yn[elementoVariante.x][elementoVariante.x] += g;
        }
        else
        {
          /*depende do passo e do valor anterior*/
          g = (2*elementoVariante.valor)/passo_simulacao;
          Yn[elementoVariante.a][elementoVariante.x] += 1;
          Yn[elementoVariante.b][elementoVariante.x] -= 1;
          Yn[elementoVariante.x][elementoVariante.a] -= 1;
          Yn[elementoVariante.x][elementoVariante.b] += 1;
          Yn[elementoVariante.x][elementoVariante.x] += g;
          Yn[elementoVariante.x][numeroVariaveis+1]+= g*(elementoVariante.jt0) + elementoVariante.vt0;
        }
      }
    }/*for*/

    #ifdef  DEBUG
      printf("Sistema apos montagem das estampas variantes no tempo. t = %g\n", tempo);
      MostrarSistema();
    #endif
}/*MontarEstampasVariantes*/

void CalcularJt0EVt0 (unsigned quantidade, int elementos[MAX_ELEM], unsigned pontoOperacao, double passo_simulacao)
{
  unsigned contador;
  elemento *elementoCL;
  double resistencia, tensaoAtual, correnteAtual;
  for (contador = 0; contador < quantidade; contador++)
  {
    elementoCL = &(netlist[elementos[contador]]);
    if (elementoCL->nome[0] == 'C')
    {
      /*tratamento do terra*/
      if (elementoCL->a == 0)
      {
        tensaoAtual = - solucaoAnterior[elementoCL->b];
        solucaoAnterior[elementoCL->a] = 0.0;
      }

      else if (elementoCL->b == 0)
      {
        tensaoAtual = solucaoAnterior[elementoCL->a];
        solucaoAnterior[elementoCL->b] = 0.0;
      }
      else
        tensaoAtual = solucaoAnterior[elementoCL->a] - solucaoAnterior[elementoCL->b];
      /*tratamento do terra*/

      if (pontoOperacao == 1)
      {
        correnteAtual = tensaoAtual/PO_CAPACITOR;
        //printf("Corrente capacitor: %lg\n", netlist[elementos[contador]].jt0);
      }
      else
      {
        resistencia = passo_simulacao/(2*elementoCL->valor);
        correnteAtual = tensaoAtual/(resistencia) - (1/resistencia)*(elementoCL->vt0) - elementoCL->jt0;
      }
      //printf("Corrente capacitor: %lg\n", netlist[elementos[contador]].jt0);
      //printf("Tensao capacitor: %lg\n", elementoCL.vt0);
    }
    else if (elementoCL->nome[0] == 'L') /*ainda pode dar merda*/
    {
      if (elementoCL->a == 0)
      {
        tensaoAtual = - solucaoAnterior[elementoCL->b];
        solucaoAnterior[elementoCL->a] = 0.0;
      }
      else if (elementoCL->b == 0)
      {
        tensaoAtual = solucaoAnterior[elementoCL->a];
        solucaoAnterior[elementoCL->b] = 0.0;
      }
      else
        tensaoAtual = solucaoAnterior[elementoCL->a] - solucaoAnterior[elementoCL->b];
      correnteAtual = solucaoAnterior[elementoCL->x];
    }
    elementoCL->vt0 = tensaoAtual;
    elementoCL->jt0 = correnteAtual;
  }
}/*calcular jt0 e vt0*/

/* Rotina que monta as estampas dos elementos invariantes do circuito */
void MontarEstampasInvariantes()
{
  //ZerarSistema();
	for (i=1; i<=numeroElementos; i++)
	{
		tipo=netlist[i].nome[0];
		if (tipo=='R')
		{
			g = 1/netlist[i].valor;
			YnInvariantes[netlist[i].a][netlist[i].a] += g;
			YnInvariantes[netlist[i].b][netlist[i].b] += g;
			YnInvariantes[netlist[i].a][netlist[i].b] -= g;
			YnInvariantes[netlist[i].b][netlist[i].a] -= g;
		}
		else if (tipo=='G')
		{
			g = netlist[i].valor;
			YnInvariantes[netlist[i].a][netlist[i].c] += g;
			YnInvariantes[netlist[i].b][netlist[i].d] += g;
			YnInvariantes[netlist[i].a][netlist[i].d] -= g;
			YnInvariantes[netlist[i].b][netlist[i].c] -= g;
		}

    /*So preenche se for DC*/
    else if (strcmp(netlist[i].tipo_fonte, "DC") == 0)
    {
      netlist[i].valor = netlist[i].fonte_dc.valor;
      g = netlist[i].valor;
      if (tipo =='I')
      {
        YnInvariantes[netlist[i].a][numeroVariaveis+1]-=g;
        YnInvariantes[netlist[i].b][numeroVariaveis+1]+=g;
      }
      else if (tipo == 'V')
      {
        YnInvariantes[netlist[i].a][netlist[i].x]+=1;
        YnInvariantes[netlist[i].b][netlist[i].x]-=1;
        YnInvariantes[netlist[i].x][netlist[i].a]-=1;
        YnInvariantes[netlist[i].x][netlist[i].b]+=1;
        YnInvariantes[netlist[i].x][numeroVariaveis+1]-=g;
      }
    }
		else if (tipo=='E')
		{
			g = netlist[i].valor;
			YnInvariantes[netlist[i].a][netlist[i].x] += 1;
			YnInvariantes[netlist[i].b][netlist[i].x] -= 1;
			YnInvariantes[netlist[i].x][netlist[i].a] -= 1;
			YnInvariantes[netlist[i].x][netlist[i].b] += 1;
			YnInvariantes[netlist[i].x][netlist[i].c] += g;
			YnInvariantes[netlist[i].x][netlist[i].d] -= g;
		}
		else if (tipo=='F')
		{
			g = netlist[i].valor;
			YnInvariantes[netlist[i].a][netlist[i].x] += g;
			YnInvariantes[netlist[i].b][netlist[i].x] -= g;
			YnInvariantes[netlist[i].c][netlist[i].x] += 1;
			YnInvariantes[netlist[i].d][netlist[i].x] -= 1;
			YnInvariantes[netlist[i].x][netlist[i].c] -= 1;
			YnInvariantes[netlist[i].x][netlist[i].d] += 1;
		}
		else if (tipo=='H')
		{
			g = netlist[i].valor;
			YnInvariantes[netlist[i].a][netlist[i].y] += 1;
			YnInvariantes[netlist[i].b][netlist[i].y] -= 1;
			YnInvariantes[netlist[i].c][netlist[i].x] += 1;
			YnInvariantes[netlist[i].d][netlist[i].x] -= 1;
			YnInvariantes[netlist[i].y][netlist[i].a] -= 1;
			YnInvariantes[netlist[i].y][netlist[i].b] += 1;
			YnInvariantes[netlist[i].x][netlist[i].c] -= 1;
			YnInvariantes[netlist[i].x][netlist[i].d] += 1;
			YnInvariantes[netlist[i].y][netlist[i].x] += g;
		}
		else if (tipo=='O')
		{
			YnInvariantes[netlist[i].a][netlist[i].x] += 1;
			YnInvariantes[netlist[i].b][netlist[i].x] -= 1;
			YnInvariantes[netlist[i].x][netlist[i].c] += 1;
			YnInvariantes[netlist[i].x][netlist[i].d] -= 1;
		}
    #ifdef DEBUG
    		/* Opcional: Mostra o sistema apos a montagem da estampa */
    		printf("Sistema apos a estampa de %s\n",netlist[i].nome);
    		for (k=1; k<=numeroVariaveis; k++)
    		{
    			for (j=1; j<=numeroVariaveis+1; j++)
    				if (Yn[k][j]!=0)
    					printf("%+4.3f ",Yn[k][j]);
    				else printf(" ..... ");
    			printf("\n");
    		}
    		getch();
    #endif
	}/*for*/
}/*MontarEstampasInvariantes*/

void ListarTudo ()
{
  printf("Variaveis internas: \n");
  for (i=0; i<=numeroVariaveis; i++)
    printf("%d -> %s\n",i,lista[i]);
  getch();
  printf("Netlist interno final\n");
  for (i=1; i<=numeroElementos; i++)
  {
    tipo=netlist[i].nome[0];
    if (tipo=='R' || tipo=='I' || tipo=='V')
    {
      printf("%s %d %d %g\n",netlist[i].nome,netlist[i].a,netlist[i].b,netlist[i].valor);
    }
    else if (tipo=='G' || tipo=='E' || tipo=='F' || tipo=='H')
    {
      printf("%s %d %d %d %d %g\n",netlist[i].nome,netlist[i].a,netlist[i].b,netlist[i].c,netlist[i].d,netlist[i].valor);
    }
    else if (tipo=='O')
    {
      printf("%s %d %d %d %d\n",netlist[i].nome,netlist[i].a,netlist[i].b,netlist[i].c,netlist[i].d);
    }
    if (tipo=='V' || tipo=='E' || tipo=='F' || tipo=='O')
      printf("Corrente jx: %d\n",netlist[i].x);
    else if (tipo=='H')
      printf("Correntes jx e jy: %d, %d\n",netlist[i].x,netlist[i].y);
  }
  getch();
  /* Monta o sistema nodal modificado */
  printf("O circuito tem %d nos, %d variaveis e %d elementos\n",numeroNos,numeroVariaveis,numeroElementos);
  getch();
}/*ListarTudo*/

void AcrescentarVariaveis()
{
  /* Acrescenta variaveis de corrente acima dos nos, anotando no netlist */
  numeroNos=numeroVariaveis;
  for (i=1; i<=numeroElementos; i++)
  {
    tipo=netlist[i].nome[0];
    if (tipo=='V' || tipo=='E' || tipo=='F' || tipo=='O')
    {
      numeroVariaveis++;
      if (numeroVariaveis>MAX_NOS)
      {
        printf("As correntes extra excederam o numero de variaveis permitido (%d)\n",MAX_NOS);
        exit(1);
      }
      strcpy(lista[numeroVariaveis],"j"); /* Tem espaco para mais dois caracteres */
      strcat(lista[numeroVariaveis],netlist[i].nome);
      netlist[i].x=numeroVariaveis;
    }
    else if (tipo=='H')
    {
      numeroVariaveis=numeroVariaveis+2;
      if (numeroVariaveis>MAX_NOS)
      {
        printf("As correntes extra excederam o numero de variaveis permitido (%d)\n",MAX_NOS);
        exit(1);
      }
      strcpy(lista[numeroVariaveis-1],"jx"); strcat(lista[numeroVariaveis-1],netlist[i].nome);
      netlist[i].x=numeroVariaveis-1;
      strcpy(lista[numeroVariaveis],"jy"); strcat(lista[numeroVariaveis],netlist[i].nome);
      netlist[i].y=numeroVariaveis;
    }
  }
}/*AcrescentarVariaveis*/

void ResolverPontoOperacao (unsigned contadorVariantes, unsigned contadorNaoLineares, double passo_simulacao)
{
  MontarEstampasVariantes(elementosVariantes, 0, contadorVariantes, passo_simulacao, 1); /*ponto de operacao*/
  #ifdef DEBUG
    MostrarSistema();
  #endif
  if (contadorNaoLineares == 0) /*se nao tem elementos nao lineares, resolve normalmente*/
  {
    printf("Entra no if de nao tem elementos nao lineares\n");
    if (ResolverSistema()) /*calculo do ponto de operacao*/
    {
      getch();
      exit(0);
    }
    ArmazenarResultadoAnterior(); /*armazeno as solucoes anteriores num vetor solucaoAnterior*/
    CalcularJt0EVt0(contadorVariantes, elementosVariantes, 1, passo_simulacao); /*armazeno as correntes nos capacitores e as tensoes nos indutores do resultado anterior*/
  }
  else /*se tiver elementos nao lineares, resolve com NR*/
  {
    /*Newton-Raphson que depende da solucao anterior*/
  }
} /*ResolverPontoOperacao*/

int main(void)
{
  system("cls");
  printf("Programa demonstrativo de analise nodal modificada no dominio do tempo\n\n");
  printf("Desenvolvido por: - Fabiana Ferreira Fonseca \n");
  printf("\t\t  - Leonardo Barreto Alves \n");
  printf("\t\t  - Vinicius dos Santos Mello \n\n");
  printf("\t\t  __/\\  /\\  /\\  /\\__\n");
  printf("\t\t      \\/  \\/  \\/    \n");
  printf("Versao %s\n",versao);
  denovo:
  /* Leitura do netlist */
  numeroElementos=0;
  numeroVariaveis=0;
  strcpy(lista[0],"0");
  printf("Nome do arquivo com o netlist (ex: mna.net): ");
  scanf("%50s",nomeArquivo);
  arquivo=fopen(nomeArquivo,"r");
  if (arquivo==0)
  {
    printf("Arquivo %s inexistente\n",nomeArquivo);
    goto denovo;
  }

  LerNetlist(arquivo);
  AcrescentarVariaveis();
  ListarTudo();

  FILE *arquivoSaida = fopen(NOME_ARQUIVO_SAIDA, "w");
  /*Escreve o header do arquivo de saida*/
  fprintf(arquivoSaida, "%s", "t");
  for (i=1; i<=numeroVariaveis; i++)
    fprintf(arquivoSaida, " %s", lista[i]);
  fprintf(arquivoSaida, "\n");

  ZerarSistema();
  MontarEstampasInvariantes();
  CopiarEstampaInvariante();
  printf("Sistema apos estampas invariantes:\n");
  MostrarSistema();
  ResolverPontoOperacao(contadorElementosVariantes, contadorElementosNaoLineares, passo_simulacao);
  MostrarSolucaoAtual();

  /*Analise no tempo*/
  for (tempo_atual = passo_simulacao; tempo_atual < tempo_simulacao; tempo_atual += passo_simulacao) /*começa em 0 ou em 0 + passo?*/
  {
    MontarEstampasVariantes(elementosVariantes, tempo_atual, contadorElementosVariantes, passo_simulacao, 0);

    /*Newton-Raphson com parametros do sistema inicial (ponto de operacao)*/

    /* Resolve o sistema */
    if (ResolverSistema())
    {
      getch();
      exit(0);
    }
    ArmazenarResultadoAnterior();
    CalcularJt0EVt0(contadorElementosVariantes, elementosVariantes, 0, passo_simulacao);  /*armazeno as correntes nos capacitores e as tensoes nos indutores do resultado anterior*/

    /*Escreve no arquivo de saida*/
    fprintf(arquivoSaida,"%lg", tempo_atual);
    for (i=1; i<=numeroVariaveis; i++)
    {
      fprintf(arquivoSaida," %lg", Yn[i][numeroVariaveis+1]);
    }
    fprintf(arquivoSaida,"\n");
  }/*for analise no tempo*/

  /*Fecha arquivo*/
  fclose(arquivoSaida);

#ifdef DEBUG
  /* Opcional: Mostra o sistema resolvido */
  printf("Sistema resolvido:\n");
  for (i=1; i<=numeroVariaveis; i++)
  {
      for (j=1; j<=numeroVariaveis+1; j++)
        if (Yn[i][j]!=0) printf("%+3.1f ",Yn[i][j]);
        else printf(" ... ");
      printf("\n");
    }
  getch();
#endif
  /* Mostra solucao */
  printf("Solucao:\n");
  strcpy(txt,"Tensao");
  for (i=1; i<=numeroVariaveis; i++)
  {
    if (i==numeroNos+1) strcpy(txt,"Corrente");
    printf("%s %s: %g\n",txt,lista[i],Yn[i][numeroVariaveis+1]);
  }
  getch();
  return 0;
}
